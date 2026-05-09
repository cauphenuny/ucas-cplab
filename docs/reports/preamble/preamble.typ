#import "theme.typ"
#import theme: *
#let split-full = pad(top: 1.5em, bottom: 1.5em)[#align(center, line(stroke: 0.04em, length: 100%))]
#let split-semi = align(center, line(stroke: 0.04em, length: 60%))
#let split = split-semi
#import "meta.typ": meta
#let conf(title: none, doc) = theme.conf(
  title: title,
  semister: meta.semester,
  course: meta.course,
  author: meta.author,
  doc,
)

