"""Guards the preschool word list that the artwork, audio and firmware are built from.

`content/curriculum.json` is the single source for all three, so the word order
asserted here is the same order `word_image()` indexes and the same order the
audio compiler numbers its clips.
"""
import json
import re
import struct
import sys
import unittest
import zlib
from pathlib import Path

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R / 'tools'))

UNITS = {
    '水果': ['apple', 'banana', 'orange', 'pear', 'grape', 'peach', 'lemon', 'tomato', 'mango', 'cherry'],
    '动物': ['duck', 'cat', 'dog', 'pig', 'cow', 'bird', 'fish', 'rabbit', 'bear', 'frog'],
    '颜色': ['red', 'blue', 'yellow', 'green', 'black', 'white', 'pink', 'brown', 'purple', 'gray'],
    '数字': ['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine', 'ten'],
    '身体': ['eye', 'ear', 'nose', 'mouth', 'hand', 'foot', 'face', 'hair', 'tooth', 'arm'],
    '日常用品': ['book', 'ball', 'cup', 'bed', 'hat', 'shoe', 'bag', 'spoon', 'key', 'clock'],
}
CELL = 112  # the card picture size word_art.c is generated at


class CurriculumTests(unittest.TestCase):
    def setUp(self):
        self.c = json.loads((R / 'content/curriculum.json').read_text())
        self.words = [w for u in self.c['units'] for w in u['words']]

    def test_six_themed_units_of_ten_words(self):
        self.assertEqual([u['title_zh'] for u in self.c['units']], list(UNITS))
        for unit in self.c['units']:
            self.assertEqual([w[0] for w in unit['words']], UNITS[unit['title_zh']])
            for en, zh in unit['words']:
                self.assertTrue(en.isascii() and en.islower() and zh.strip())

    def test_there_are_no_lessons_left(self):
        """The book is a card deck: no lesson groups, so no sentence anywhere.

        A stray `lessons` key would silently re-enter the audio id space through
        compile_content.py and shift every clip the firmware was built with.
        """
        self.assertNotIn('lessons', self.c)
        for unit in self.c['units']:
            self.assertEqual(set(unit), {'title_zh', 'words'}, unit['title_zh'])

    def test_compiled_header_agrees_with_the_source(self):
        header = (R / 'main/content.h').read_text()
        for name, value in [('WORD_COUNT', len(self.words)),
                            ('AUDIO_COUNT', len(self.words)),
                            ('UNIT_COUNT', len(self.c['units']))]:
            self.assertIn('#define %s %d\n' % (name, value), header, name)
        for gone in ('LESSON_COUNT', 'SENTENCE_COUNT', 'AUDIO_WORD_COUNT'):
            self.assertNotIn(gone, header, 'the sentence build is gone for good')
        self.assertIn('#define PREVIOUS_WORD_COUNT 0\n', header)

    def test_audio_ids_are_contiguous_and_are_the_words(self):
        audio = json.loads((R / 'content/audio-texts.json').read_text())
        self.assertEqual([a['id'] for a in audio], list(range(1, len(audio) + 1)))
        self.assertTrue(all(a['text'].strip() for a in audio))
        self.assertEqual(len({a['text'] for a in audio}), len(audio),
                         'a duplicate reading would waste an audio id')
        self.assertEqual([a['text'] for a in audio], [w for w, _ in self.words])

    def test_the_embedded_voice_pack_is_this_books_pack(self):
        """Catches a stale voice.bin: the header must carry the current version.

        The firmware refuses to play a pack whose CONTENT_VERSION differs, so a
        forgotten `tools/say_audio.py` run after a content change would ship a
        book that says "sound unavailable" on every card.
        """
        header = (R / 'main/content.h').read_text()
        version = int(re.search(r'CONTENT_VERSION (\d+)u', header)[1])
        count = int(re.search(r'AUDIO_COUNT (\d+)', header)[1])
        blob = (R / 'assets/audio/voice.bin').read_bytes()
        magic, fmt, first, clips, frame, rate, content = struct.unpack_from('<4sHHHHII', blob, 0)
        self.assertEqual(magic, b'EPOP')
        self.assertEqual(fmt, 1)
        self.assertEqual((first, clips), (1, count))
        self.assertEqual((frame, rate), (320, 16000))
        self.assertEqual(content, version)
        manifest = json.loads((R / 'assets/audio/manifest.json').read_text())
        self.assertEqual(manifest['content_version'], version)
        self.assertEqual(manifest['clips'], count)
        self.assertEqual(manifest['bytes'], len(blob))

    def test_there_is_no_sentence_pack_or_partition(self):
        self.assertFalse((R / 'assets/audio-full').exists())
        table = (R / 'partitions.csv').read_text()
        self.assertNotIn('voices', table)

    def test_artwork_covers_every_word_at_the_card_size(self):
        art = (R / 'main/word_art.c').read_text()
        self.assertEqual(len(re.findall(r'\.cf=LV_COLOR_FORMAT_RGB565', art)), len(self.words))
        self.assertEqual(art.count('.w=%d,.h=%d,.stride=%d' % (CELL, CELL, CELL * 2)), len(self.words))
        self.assertNotIn('lesson_image', art)
        self.assertNotIn('lesson_word', art)
        self.assertIn('_Static_assert(sizeof(word_images)/sizeof(*word_images)==WORD_COUNT', art)

    def test_no_fabricated_word_migration(self):
        # The preschool list has no honest correspondence to the 720-word textbook
        # list, so any mapping would invent "the same word" where there is none.
        self.assertNotIn('word_migration', self.c)
        self.assertIs(self.c['complete'], False)

    def test_every_word_carries_one_bare_transcription(self):
        """The card prints `word_ipa[]` verbatim between slashes, so it must be bare."""
        ipa = self.c['ipa']
        self.assertEqual(set(ipa), {w for w, _ in self.words},
                         'the ipa map must cover the word list exactly, with no extras')
        for word, value in ipa.items():
            self.assertFalse(any(ch.isspace() for ch in value), word)
            self.assertNotIn('/', value, word)
            self.assertTrue(value.isprintable(), word)
            # ɡ and ɹ are the strict IPA letters, but plain ASCII g/r are easier for a
            # parent to read and stay inside the part of every subset that cannot go stale.
            self.assertNotIn('ɡ', value, word)
            self.assertNotIn('ɹ', value, word)

    def test_compiled_ipa_table_follows_the_word_order(self):
        code = (R / 'main/content.c').read_text()
        table = re.search(r'const char \*word_ipa\[WORD_COUNT\]=\{(.*?)\};', code, re.S)
        self.assertIsNotNone(table, 'content.c has no word_ipa table')
        self.assertEqual(json.loads('[' + table[1] + ']'),
                         [self.c['ipa'][w] for w, _ in self.words])

    def test_pronunciations_stay_out_of_the_save_and_audio_versions(self):
        """Editing a transcription must not wipe progress or invalidate the voice pack.

        `items[]` and the two version macros are hashed from the word pairs alone, so
        the ipa map has to remain a separate table rather than a third tuple element.
        """
        header = (R / 'main/content.h').read_text()
        expected = zlib.crc32(json.dumps(self.words, ensure_ascii=False).encode())
        self.assertIn('#define WORD_CONTENT_VERSION %du\n' % expected, header)
        # The upgrade that retired the sentences kept this number, which is the
        # evidence that a device's saved volume is not thrown away by it.
        self.assertEqual(expected, 628472539)


if __name__ == '__main__':
    unittest.main()
