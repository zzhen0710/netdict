#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ecdict2tsv.py — 把 ECDICT 的 SQLite 库转成本项目 netdict 的 TSV 词库。

用法：
    python ecdict2tsv.py <stardict.db> <dict.txt>

输入：
    stardict.db    ECDICT 提供的 SQLite 数据库。
                   来源包：ecdict-sqlite-28.zip
                   （https://github.com/skywind3000/ECDICT/releases）
                   解压后为 stardict.db；表名 stardict。
                   关键列：
                     word         单词
                     translation  中文释义（\\n 分隔多条）
                     tag          考试标签（空格分隔，如 "cet4 cet6 ky"）
                     collins      柯林斯星级（0-5，5 最常用）
                     oxford       是否牛津 3000 核心（0/1）
                     bnc          BNC 语料库词频排名（越小越常用；0/NULL = 不在榜）
                     frq          COCA 语料库词频排名（越小越常用；0/NULL = 不在榜）
                   （pos 列是"词频占比"，如 "n:100"，不是词性，故不用；
                     exchange / detail 等列亦未用）

输出：
    dict.txt       TSV 三段，每行一条释义：
                       <word>\\t<pos>\\t<mean>
                   一词多义 = 多行（行数 > 词数）。

过滤规则（"英语学习词典"取向）：
    1. word 必须是纯字母（可含连字符、单引号），排除含点/空格/数字的词。
    2. 只保留"在真实语料/词典里出现过的词"：
         有考试标签（tag）
         或 柯林斯星级 >= 1
         或 牛津 3000 核心（oxford = 1）
         或 有 BNC 词频排名（bnc > 0）
         或 有 COCA 词频排名（frq > 0）
       把 380 万降到约 15~25 万行（约 8~15 万词），去掉人名、地名、网络、
       极冷门词。
    3. 释义以 [人名]/[地名]/[网络] 开头的，跳过（不是词典释义）。

词性（pos）处理：
    ECDICT 的 pos 列是"词频占比"（如 "n:100"），不是词性。
    真正的词性在 translation 每一段的段首（如 "n. 接受, 接纳..."）。
    本脚本从段首提取词性（n. / vt. / adj. / [经] 等）放进 pos 列，
    剩下的正文放进 mean 列。

输出示例：
    acceptance\\tn.\\t接受, 接纳, 承认, 同意, 赞同, 容忍, 相信
    acceptance\\t[经]\\t承兑, 认付, (工程)验收
    access\\tn.\\t通路, 入口, 接近, 进入, 使用权, 发作
    access\\tvt.\\t访问, 存取, 接近, 使用
"""

import re
import sqlite3
import sys


# ---------- 过滤配置 ----------

# word 只允许：字母开头，后续字母/连字符/单引号。
# 排除含点（a.d.）、空格（ball-point pen）、数字的词。
WORD_RE = re.compile(r"^[a-zA-Z][a-zA-Z'\-]*$")

# 释义段以这些开头 → 非词典释义（人名/地名/网络），跳过。
# 注意：[医]/[化] 这类是"专业释义"，保留。
SKIP_PREFIX = ("[人名]", "[地名]", "[网络]")

# 段首词性提取：匹配 "n." / "vt." / "adj." 等（字母 + 点），
# 或 "[经]" / "[计]" 这类专业标注（方括号 + 中文）。
# 捕获组 1 = 词性标注，捕获组 2 = 剩余正文。
POS_RE = re.compile(r"^(\[[^\]]+\]|[a-zA-Z]+\.)\s+(.*)$")


def convert(db_path: str, out_path: str) -> None:
    """读 ECDICT 库，写 netdict 的 TSV 词库。"""
    conn = sqlite3.connect(db_path)
    cur  = conn.cursor()

    written = 0    # 写出的释义条数（行数）
    words   = set()  # 写出的不同单词数

    # newline="\n"：强制 LF，避免 Windows 写出 CRLF。
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        # "在语料/词典里出现过"的筛选：
        #   有考试标签 / 柯林斯星级 / 牛津核心 / BNC 词频 / COCA 词频。
        # 各列可能为 NULL，用 coalesce 归 0。
        sql = (
            "select word, translation from stardict "
            "where translation is not null and translation != '' "
            "  and ( (tag is not null and tag != '') "
            "        or coalesce(collins, 0) >= 1 "
            "        or coalesce(oxford, 0) = 1 "
            "        or coalesce(bnc, 0) > 0 "
            "        or coalesce(frq, 0) > 0 )"
        )
        for word, translation in cur.execute(sql):
            word = (word or "").strip()

            # 规则 1：word 必须是纯字母词
            if not WORD_RE.match(word):
                continue

            # translation 里 \n 分隔多条释义；每条一段
            for seg in translation.split("\n"):
                seg = seg.strip()
                if not seg:
                    continue

                # 规则 3：跳过 [人名]/[地名]/[网络] 开头的段
                if seg.startswith(SKIP_PREFIX):
                    continue

                # 提取段首词性（n. / vt. / [经] ...）
                m = POS_RE.match(seg)
                if m:
                    pos  = m.group(1)   # 词性标注
                    mean = m.group(2)   # 释义正文
                else:
                    pos  = ""           # 无词性
                    mean = seg

                # 写一行：word \t pos \t mean
                f.write(f"{word}\t{pos}\t{mean}\n")
                written += 1
                words.add(word)

    conn.close()
    print(f"done: {out_path} ({written} entries, {len(words)} words)")


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: python ecdict2tsv.py <stardict.db> <dict.txt>")
        sys.exit(1)
    convert(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()