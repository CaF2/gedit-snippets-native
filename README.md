# Snippets

Inspired on the original snippets plugin for gedit, but rewritten for C.

Currently it can load and somewhat print the snippets as the original plugin. 
It has basic support for jumping and editing in the snippets. Currently you have to press tab after the last edit to get the full snippet.

# Installation

Just follow these steps:

````
cd ~/.local/share/gedit/plugins
git clone https://github.com/CaF2/gedit-snippets-native.git
cd gedit-snippets-native
make
````

Then go into gedit -> settings -> plugins and enable this plugin

If it does not work, check that you have the gedit-devel package installed.
