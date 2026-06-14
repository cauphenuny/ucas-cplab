#import "../preamble/preamble.typ": *
#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.10": *
#show: codly-init.with()
#show: doc => conf(doc, title: "P3 实验报告")

#codly(languages: (
  rust: (name: "RIIR", icon: text(size: 0.85em)[🦀 ], color: rgb("#CE412B")),
  c: (name: "C", icon: none, color: codly-languages.c.color),
  cpp: (name: "cpp", icon: none, color: codly-languages.cpp.color),
))

= 实验内容

== 后端代码生成

#include "sections/codegen.typ"

== 优化

#include "sections/optimize.typ"

= 实验效果以及遇到的问题

== 效果展示

#include "sections/demo.typ"

== 遇到的问题与解决方案

#include "sections/problem.typ"

= 附录

== 参考文献

#{
  v(1em)
  set text(lang: "en")
  bibliography("sections/reference.bib", title: none)
}