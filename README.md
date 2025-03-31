# Snippets

Inspired on the original snippets plugin for gedit, but rewritten for C.

Currently it can load and somewhat print the snippets as the original plugin. 
However id does not currently support to edit the snippets properly or to allow to jump around in markers as with the original plugin.

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
