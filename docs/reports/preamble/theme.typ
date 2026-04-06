#import "@preview/numbly:0.1.0": numbly
#import "@preview/tablem:0.2.0": tablem, three-line-table
#import "@preview/ctheorems:1.1.3": thmrules
#import "@preview/theorion:0.3.3"
#import "@preview/cetz:0.4.2"
#import "@preview/cmarker:0.1.7"
#import "@preview/codly:1.3.0": *
#import "@preview/codly-languages:0.1.1": *
#import "@preview/theorion:0.5.0": *
#import cosmos.rainbow: *
// #import cosmos.rainbow: *
// #import cosmos.clouds: *

#let split-line = align(center, line(length: 100%, stroke: 0.05em))
#let revise(content) = text(green, content)
// #let wrong(content) = text(red, content)
#let wrong(content) = text(fill: red, $cancel(#content)$)

#let conf(
  title: none,
  sub-title: none,
  semister: none,
  course: none,
  author: none,
  numbering: numbly("{1:一}、", default: "1.1  "),
  enum-numbering: "1.a.i.",
  lang: "zh",
  doc,
) = {
  //import "@preview/cuti:0.2.1": show-cn-fakebold
  //show: show-cn-fakebold

  set text(font: ("Libertinus Serif", "Songti SC"), lang: lang)
  show emph: text.with(font: ("Libertinus Serif", "STKaiti"))
  show: thmrules
  show: codly-init.with()

  set heading(numbering: numbering)

  set image(width: 75%)
  set page(
    margin: (x: 4em, y: 6em),
    header: context {
      set text(0.9em)
      let page_number = here().page()
      stack(
        spacing: 0.2em,
        (
          if (page_number == 1) { align(right, emph(semister + " " + course)) } else {
            align(right, [#emph(author) | #emph(course) | #emph(title)])
          }
        ),
        v(0.3em),
        line(length: 100%, stroke: 1pt),
        v(.15em),
        line(length: 100%, stroke: .5pt),
      )
    },
    numbering: "1",
    footer: context {
      let page_number = here().page()
      let total_pages = counter(page).final().first()
      align(center)[Page #page_number of #total_pages]
    },
  )

  show ref: it => {
    let eq = math.equation
    let el = it.element
    if el != none and el.func() == eq {
      numbering(el.numbering, ..counter(eq).at(el.location()))
    } else {
      it
    }
  }
  show ref: r => text(blue, r)

  set enum(numbering: enum-numbering)

  // show math.equation: eq => $eq$
  show math.equation: set text(font: ("New Computer Modern Math", "Songti SC"))
  let numbered-eq(content) = math.equation(
    block: true,
    numbering: "(1.1.1)",
    content,
  )

  show table.cell.where(y: 0): strong
  set table(
    align: center + horizon,
    inset: 0.6em,
    stroke: 0.05em,
  )
  show raw: set text(font: ("Menlo", "FiraCode Nerd Font"))
  show raw.where(block: true): set text(size: 0.8em)

  show: show-theorion

  align(center, text(1.7em)[*#course* *#title*])
  align(right, emph(author))
  doc
}
