export const restrictedSyntax = [
  {
    selector: "Literal[value=/\\b(?:bg|text|border|ring|from|to|via|fill|stroke|outline|decoration|divide|placeholder|accent|caret|shadow)-(slate|gray|zinc|neutral|stone|red|orange|amber|yellow|lime|green|emerald|teal|cyan|sky|blue|indigo|violet|purple|fuchsia|pink|rose)-\\d+\\b/]",
    message: "Use semantic design tokens from @mosaic/tokens instead of Tailwind palette colors."
  },
  {
    selector: "Literal[value=/#[0-9a-fA-F]{3,8}/]",
    message: "Do not hardcode hex colors in class strings; use @mosaic/tokens semantic utilities."
  },
  {
    selector: "Literal[value=/\\b(?:bg|text|border|ring|from|to|via)-(neutral|blue|green|amber|red)\\b/]",
    message: "Primitive palette utilities are not public API; use semantic design tokens."
  },
  {
    selector: "Literal[value=/\\brounded(?:-[tblr]|-[tb][lr])?-(?!(sm|md|lg|full)\\b)[a-z0-9-]+\\b/]",
    message: "Radius tokens are strictly limited to: sm, md, lg, full."
  },
  {
    selector: "Literal[value=/\\bshadow-(?!(sm|md|lg)\\b)[a-z0-9-]+\\b/]",
    message: "Shadow tokens are strictly limited to: sm, md, lg."
  },
  {
    selector: "Literal[value=/\\bfont-(?!(normal|medium|semibold|bold)\\b|sans\\b|display\\b|mono\\b)[a-z0-9-]+\\b/]",
    message: "Font tokens are strictly limited to configured families and weights."
  }
];
