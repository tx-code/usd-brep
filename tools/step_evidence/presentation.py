# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Offline evidence presentation; validation scopes are supplied explicitly by stage adapters."""
from __future__ import annotations

import html
import json


def page(title: str, body: str, description: str) -> bytes:
    return ("<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' "
            "content='width=device-width'><title>" + html.escape(title) + "</title><style>"
            "body{max-width:1100px;margin:32px auto;padding:0 20px;font:16px/1.7 system-ui;color:#243441}"
            "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f3f5f7;padding:16px}"
            "table{border-collapse:collapse;width:100%;display:block;overflow-x:auto}"
            "td,th{padding:8px;border-bottom:1px solid #ddd;text-align:left}a{color:#185c85}</style><h1>" +
            html.escape(title) + "</h1><p>" + html.escape(description) + "</p>" + body + "</html>\n").encode("utf-8")


def case_body(ticket: str, observation_file: str, expected: dict, observed: dict) -> str:
    return (f"<p>{ticket}当前范围合同：Matched；完整导入合同：NotChecked。</p>"
            f"<p><a href='input.step'>实际输入</a> · <a href='{observation_file}'>真实C++输出</a> · "
            "<a href='invocation.json'>实际调用及二进制身份</a> · <a href='comparison.json'>独立比较</a></p>"
            "<h2>完整导入期望（后续票，未验收）</h2><pre>" +
            html.escape(json.dumps(expected, ensure_ascii=False, indent=2)) + "</pre>"
            "<h2>本次实际检查</h2><pre>" +
            html.escape(json.dumps(observed, ensure_ascii=False, indent=2)) + "</pre>")
