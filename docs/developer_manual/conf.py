# Configuration file for the standalone build of the Pulp developer manual.
#
# When embedded into the main GVSoC developer manual (via the engine docs'
# target_docs extension), this file is ignored and only the rst pages are
# pulled in; the extension list below is still honoured by being merged
# into the host manual's extensions.

project = 'GVSoC Pulp'
copyright = '2024, GreenWaves Technologies / PULP platform'
author = 'GVSoC'

extensions = []

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

html_theme = 'sphinx_rtd_theme'
html_use_smartypants = False
smartquotes = False
