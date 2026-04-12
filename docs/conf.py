#!/usr/bin/env python3

import os

project = "GridDyn"
copyright = (
    "2014-2026 Lawrence Livermore National Security. "
    "See the top-level NOTICE for additional details."
)
author = "GridDyn contributors"

extensions = [
    "myst_parser",
]

source_suffix = {
    ".md": "markdown",
}

master_doc = "index"
language = "en"

exclude_patterns = [
    "_build",
    "Thumbs.db",
    ".DS_Store",
    "GridDyn style guide.docx",
    "presentations",
    "state_estimation.tex",
    "mainpage.dox",
    "simple_help.txt",
]

myst_heading_anchors = 3

templates_path = []
html_static_path = []
html_theme = "sphinx_rtd_theme"
html_title = "GridDyn Documentation"

html_context = {}
if os.environ.get("READTHEDOCS", "") == "True":
    html_context["READTHEDOCS"] = True
