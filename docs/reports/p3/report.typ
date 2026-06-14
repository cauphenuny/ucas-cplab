#import "../preamble/preamble.typ": *
#show: doc => conf(doc, title: "P3 实验报告")

= 实验内容

== 后端代码生成

#include "sections/codegen.typ"

== 优化

#include "sections/optimize.typ"

= 实验效果

#include "sections/demo.typ"

= 实验中遇到的问题与解决方案

#include "sections/problem.typ"

= 附录

== 参考文献

#{
  v(1em)
  set text(lang: "en")
  bibliography("sections/reference.bib", title: none)
}