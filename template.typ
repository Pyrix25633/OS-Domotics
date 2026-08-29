#let cover(
  name: "",
  course: "",
  professors: (),
  university: "",
  group: (),
  date: ""
) = [
  #set page(background: rect(width: 100%, height: 100%, fill: teal.lighten(40%)))
  #align(center + horizon)[
    
    #title()

    #v(0.5cm)
    #line(length: 100%)
    #v(0.5cm)

    #set text(size: 20pt)

    #underline[*#course*]

    #set text(size: 18pt)

    *Professors*:\
    #for p in professors {
      [#p\ ]
    }

    #line(length: 100%, stroke: navy + 0.5pt)

    University of *#university*

    #date

    #line(length: 100%, stroke: navy + 0.5pt)
    
    *Group 22*:\
    #for m in group {
      [#eval(m, mode: "markup")\ ]
    }
  ]

  #pagebreak()
]

#let indents = (
  lists: 25pt
)

#let insets = (
  blocks: 8pt
)

#let radiuses = (
  blocks: 5pt
)

#let header-spacing = it => {
  it
  v(7pt)
};

#let metadata = (
  name: "Group Project 2: Domotics",
  course: "Operating Systems",
  professors: ("Domenico Siracusa", "Matteo Franzil"),
  university: "Trento",
  group: (
    "Mattia Biral --- 252925",
    "Leonardo Paiola --- 252756",
    "Elisa Potrich --- 250782"
  ),
  date: "September 2026"
)

#let template(t: [], doc) = {
  set document(author: ("Biral Mattia", "Potrich Elisa", "Paiola Leonardo"), title: metadata.name)
  set page(
    paper: "a4",
    margin: (x: 1.2cm, y: 1.2cm)
  )
  set text(size: 12pt, lang: "en")
  set list(
    indent: indents.lists,
    marker: ([•], [-])
  )
  set enum(indent: indents.lists)
  set scale(reflow: true)
  set grid(columns: (1fr, auto), gutter: 15pt)

  show heading.where(level: 1): it => {
    it
    v(-15pt)
    line(length: 100%, stroke: (paint: gray, thickness: 0.5pt))
  }
  show heading.where(level: 2): header-spacing
  show heading.where(level: 3): header-spacing
  show heading: set text(size: 1.2em)

  set table(
    fill: (x, y) =>
      if y == 0 {
        blue.lighten(20%)
      },
    align: horizon + center,
    inset: 7pt,
    stroke: 0.7pt
  )
  show table.cell.where(y: 0): strong

  cover(..metadata)
  
  align(center, title(t))

  outline(indent: 30pt)

  pagebreak()

  set page(numbering: "1")
  counter(page).update(1)

  doc
}