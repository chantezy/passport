# 历史内容查找记录

> 下文是 Demo 阶段寻找教材源文件的历史记录，**不代表当前 v0.5.3 内容**。它记录的那套教材词表、听力栏目与小熊素材已在 v0.5.3 全部从仓库移除（见 `docs/sources.md` 的「历史」一节），本文件只作为当时的检索留痕保留。当前来源与授权范围以 `docs/sources.md` 和项目 README 为准。
>
> 本文提到的「72 词 24 句」是更早一版的交付范围，与 v0.5.3 无关：当前词表是自编的 6 单元 60 词，**没有句子**——72 句极简单句连同其音频和 `voices` 分区已在本次一并删除，只在下方历史记录里留痕。

# 教材核对与内容范围

2026-09-21 按用户所给绿色封面核对，匹配科学普及出版社／北京市仁爱教育研究所七年级上册2024修订版；网上同封面标注2024，用户照片未显示版权页，不能确定具体印次。

- 官方编写机构介绍新版 Unit 1： https://new.renai-edu.com/zixundongtai/349.html
- 六单元目录及同款封面： https://yy.suyang123.com/chuzhong/qinianji/rakpb_2024shangce.html
- 对照目录： https://dzkbxz.com/book/1067/
- 书目信息： https://www.zhihailib.com/book/256731 ，标注2024-08，ISBN 9787110108130（第三方书目，未以实物版权页复核）。

六单元依次为 Let's Be Friends! / Meet My Family! / Our Colorful School Life / Fun in the Sun! / Love Mother Nature! / Celebrating the Big Days。

已逐一读取英语朗读宝 listen1…listen6 页的课文转录。页面底部“必背单词”混入其他单元词，不直接采信；词汇从正文语境和对应单元可核对的主题词中精选，部分词还原为词典形式，如 grandparents、ski、weekend。中文释义重新校订，不照搬网页中“play football”翻译成“弹”的错误。

版本交付范围：每单元12词、4个原创配套练习句，共72词24句。不是整册完整必背词表，不收录整篇课文，不使用出版社原声。听力句子根据各单元主题重新编写，不宣称为教材原句。来源链接逐单元记录于 curriculum.json。研究页面只保留在本地研究目录，不放进发布源码包。

小熊由用户提供的蜜蜂装小熊图片提取，经 imagegen 清理背景；书封面用于教材识别，未当成课文内容。语音使用本机Qwen3-TTS CustomVoice Ryan英语预设声线，无音色克隆，无语音上传。

## 2026-09-21 源文件查找进展

- 教材ISBN进一步对照闽江学院官方教材目录，七上初中英语为科学普及出版社2024年8月第1版，9787110108130：https://www.mju.edu.cn/jsjyxy/2025/0331/c3322a198945/page.htm 。仍未核对用户实物版权页。
- 第三方下载入口：https://www.dzkbw.org/book/5984.html ，其链接指向夸克分享。已通过浏览器确认目录中实际列出《【科普版(仁爱版)】七年级上册(2024秋版)英语电子课本-社学整理.pdf》，69.8M。具体目录：https://pan.quark.cn/s/9d0983050ac8#/list/share/adf33cc998674fec934ebea102ddcacc 。点击下载出现登录弹窗；未登录、未下载、未逐页核验完整性。
- 官方电子教材介绍：https://new.renai-edu.com/jiaocaitingliziyuan/ ，官方网页端：https://m.dd.renai-edu.com/ ，浏览器实际跳转手机号/验证码登录页。官方还提供教师激活说明与国家智慧教育平台入口，未发现已验证可直接下载的官方PDF。
- 查找下载入口不等于已获得可直接导入应用的用户提供教材；当前发布内容仍保持complete=false。
