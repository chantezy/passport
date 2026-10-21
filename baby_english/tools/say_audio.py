#!/usr/bin/env python3
"""Synthesize every clip with macOS `say`, encode to Opus, pack the EPOP resources.

This machine has no ffmpeg and no cloud TTS, so the chain is:

    say -v Samantha --data-format=LEI16@16000   ->  16 kHz mono PCM WAV
    (trim leading/trailing silence in pure Python)
    afconvert -f caff -d opus -b <bitrate>      ->  Opus in a CAF container

`afconvert` emits 20 ms SILK-WB packets (320 samples at 16 kHz), which is why the
firmware's frame length is 320 and not the 960 the previous 60 ms pipeline used.
Every packet's TOC is audited before packing: a packet that is not a single 20 ms
frame would decode to the wrong sample count and stall playback on the device, so
this refuses to produce a pack rather than ship one that will not play.

Every clip goes into a single EPOP pack, assets/audio/voice.bin, which the app
embeds in its own DROM segment -- there is no second pack and no external
partition to install.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import wave
import zlib

R = Path(__file__).resolve().parents[1]
WORK = R / "assets/audio-say"
FRAME = 320  # Opus frame at 20 ms / 16 kHz
RATE = 16000

SILK_MS = (10, 20, 40, 60)
HYBRID_MS = (10, 20)
CELT_MS = (2.5, 5, 10, 20)


def toc(packet):
    """Return (mode name, frame duration ms, frame-count code) for an Opus TOC."""
    config, code = packet[0] >> 3, packet[0] & 3
    if config < 12:
        return ("SILK %s" % ("NB", "MB", "WB")[config // 4], SILK_MS[config % 4], code)
    if config < 16:
        return ("Hybrid %s" % ("SWB", "FB")[config // 2 - 6], HYBRID_MS[config % 2], code)
    return ("CELT %s" % ("NB", "WB", "SWB", "FB")[config // 4 - 4], CELT_MS[config % 4], code)


def riff_chunks(data):
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a RIFF/WAVE file"
    pos = 12
    while pos + 8 <= len(data):
        kind, size = struct.unpack_from("<4sI", data, pos)
        yield kind, data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)  # RIFF pads odd-sized chunks


def read_wav(path):
    data = path.read_bytes()
    fmt = pcm = None
    for kind, body in riff_chunks(data):
        if kind == b"fmt ":
            fmt = body
        elif kind == b"data":
            pcm = body
    assert fmt and pcm, "%s has no fmt/data chunk" % path
    tag, channels, rate, _byte_rate, _align, bits = struct.unpack_from("<HHIIHH", fmt, 0)
    assert tag == 1 and channels == 1 and bits == 16 and rate == RATE, \
        "%s is not 16-bit mono %d Hz" % (path, RATE)
    return list(struct.unpack("<%dh" % (len(pcm) // 2), pcm[:len(pcm) // 2 * 2]))


def write_wav(path, samples):
    body = struct.pack("<%dh" % len(samples), *samples)
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVE"
                     + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16)
                     + b"data" + struct.pack("<I", len(body)) + body)


def trim(samples):
    """Drop leading/trailing silence, keeping a short guard so the onset is intact."""
    peak = max((abs(s) for s in samples), default=0)
    if not peak:
        raise SystemExit("synthesized clip is silent")
    gate = max(200, peak // 50)
    first = next((i for i, s in enumerate(samples) if abs(s) > gate), 0)
    last = next((i for i, s in enumerate(reversed(samples)) if abs(s) > gate), 0)
    head = max(0, first - RATE * 30 // 1000)  # 30 ms of lead-in
    tail = min(len(samples), len(samples) - last + RATE * 20 // 1000)  # 20 ms of tail
    assert tail > head, "trim removed the whole clip"
    return samples[head:tail]


def caf_packets(path):
    """Parse an Opus-in-CAF file into (packets, sizes, priming, valid_frames)."""
    data = path.read_bytes()
    assert data[:4] == b"caff", "%s is not a CAF file" % path
    pakt = payload = None
    pos = 8
    while pos + 12 <= len(data):
        kind, size = struct.unpack_from(">4s q".replace(" ", ""), data, pos)
        body = data[pos + 12:pos + 12 + size]
        if kind == b"pakt":
            pakt = body
        elif kind == b"data":
            payload = body
        pos += 12 + size
    assert pakt and payload, "%s has no pakt/data chunk" % path
    count, valid, priming, remainder = struct.unpack_from(">qqii", pakt, 0)
    # The audio description has bytesPerPacket == 0, so the packet table uses
    # variable-length entries: 1..5 bytes, high bit set means "more to come".
    sizes, at = [], 24
    while at < len(pakt):
        size = 0
        for _ in range(5):
            byte = pakt[at]
            at += 1
            size = (size << 7) | (byte & 0x7F)
            if not byte & 0x80:
                break
        sizes.append(size)
    assert len(sizes) == count, "%s: %d packet sizes for %d packets" % (path, len(sizes), count)
    # The data chunk opens with the CAF edit count; the Opus packets follow it.
    packets, at = [], 4
    for size in sizes:
        packet = payload[at:at + size]
        assert len(packet) == size, "%s: truncated packet" % path
        packets.append(packet)
        at += size
    assert at == len(payload), "%s: %d trailing bytes after the last packet" % (path, len(payload) - at)
    return packets, priming, valid, remainder


def pack(rows, version):
    """EPOP v1: 36-byte header + header CRC, 20-byte index entries, then payload."""
    payload, table = bytearray(), bytearray()
    for row in rows:
        table += struct.pack("<IIIHHI", len(payload), len(row["bytes"]), row["samples"],
                             row["skip"], 0, zlib.crc32(row["bytes"]))
        payload += row["bytes"]
    header = struct.pack("<4sHHHHIIIIII", b"EPOP", 1, rows[0]["id"], len(rows), FRAME, RATE,
                         version, len(table), len(payload), zlib.crc32(table), zlib.crc32(payload))
    assert len(header) == 36
    return header + struct.pack("<I", zlib.crc32(header)) + table + payload


def synthesize(text, voice, rate, wav):
    subprocess.run(["say", "-v", voice, "-r", str(rate), "-o", str(wav),
                    "--data-format=LEI16@16000", text], check=True)


def encode(text, args):
    key = hashlib.sha256(text.encode()).hexdigest()[:16]
    raw = WORK / "raw" / ("%s.wav" % key)
    trimmed = WORK / "trim" / ("%s.wav" % key)
    caf = WORK / ("caf-%d" % args.bitrate) / ("%s.caf" % key)
    for directory in (raw.parent, trimmed.parent, caf.parent):
        directory.mkdir(parents=True, exist_ok=True)
    if not raw.exists():
        synthesize(text, args.voice, args.rate, raw)
    samples = trim(read_wav(raw))
    write_wav(trimmed, samples)
    stamp = caf.with_suffix(".sha256")
    digest = hashlib.sha256(trimmed.read_bytes()).hexdigest()
    if not caf.exists() or not stamp.exists() or stamp.read_text() != digest:
        subprocess.run(["afconvert", "-f", "caff", "-d", "opus", "-b", str(args.bitrate),
                        str(trimmed), str(caf)], check=True)
        stamp.write_text(digest)

    packets, priming, valid, remainder = caf_packets(caf)
    modes = set()
    for i, packet in enumerate(packets):
        mode, duration, code = toc(packet)
        if code != 0 or duration != 20:
            raise SystemExit("%r packet %d is %s/%sms/code %d, not a single 20 ms frame"
                             % (text, i, mode, duration, code))
        modes.add(mode)
    frames = len(packets)
    if valid != len(samples):
        raise SystemExit("%r: CAF reports %d source frames, the trimmed WAV has %d"
                         % (text, valid, len(samples)))
    if not valid + priming <= frames * FRAME < valid + priming + FRAME:
        raise SystemExit("%r: %d frames cannot carry %d samples + %d priming"
                         % (text, frames, valid, priming))
    assert 0 <= priming < FRAME, "%r: priming %d does not fit the 16-bit skip field" % (text, priming)
    payload = b"".join(struct.pack("<H", len(p)) + p for p in packets)
    return dict(bytes=payload, samples=valid, skip=priming, modes=modes, frames=frames,
                remainder=remainder, bytes_caf=len(payload))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default="Samantha")
    ap.add_argument("--rate", type=int, default=120, help="words per minute; slower for children")
    ap.add_argument("--bitrate", type=int, default=24000, help="bits/s of the Opus encoder")
    args = ap.parse_args()

    rows = json.loads((R / "content/audio-texts.json").read_text())
    order = [r["id"] for r in rows]
    assert order == list(range(1, len(rows) + 1)), "audio ids must be 1..n with no gaps"
    header = (R / "main/content.h").read_text()
    count = int(re.search(r"AUDIO_COUNT (\d+)", header)[1])
    version = int(re.search(r"CONTENT_VERSION (\d+)", header)[1])
    assert count == len(rows), "content.h lists %d clips, audio-texts.json has %d" % (count, len(rows))

    encoded, seconds, all_modes = [], 0.0, set()
    for i, row in enumerate(rows):
        result = encode(row["text"], args)
        all_modes |= result["modes"]
        seconds += result["samples"] / RATE
        encoded.append(dict(id=row["id"], text=row["text"], bytes=result["bytes"],
                            samples=result["samples"], skip=result["skip"]))
        if i % 20 == 0 or i == len(rows) - 1:
            print("ENCODE %d/%d  %.1fs  %.2f MB" % (i + 1, len(rows), seconds,
                  sum(len(r["bytes"]) for r in encoded) / 1e6), flush=True)

    blob = pack(encoded, version)
    (R / "assets/audio").mkdir(parents=True, exist_ok=True)
    (R / "assets/audio/voice.bin").write_bytes(blob)

    manifest = dict(voice=args.voice, rate=args.rate, bitrate=args.bitrate, codec="Opus",
                    modes=sorted(all_modes), sample_rate=RATE, frame_samples=FRAME,
                    frame_ms=20, content_version=version, clips=len(encoded),
                    seconds=round(seconds, 2), bytes=len(blob),
                    sha256=hashlib.sha256(blob).hexdigest(),
                    items=[{k: v for k, v in r.items() if k != "bytes"} for r in encoded])
    (R / "assets/audio/manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print("Wrote voice.bin (%d clips, %d bytes); %.1f s total"
          % (len(encoded), len(blob), seconds))


if __name__ == "__main__":
    sys.exit(main())
