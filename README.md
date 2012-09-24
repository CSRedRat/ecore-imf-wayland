Wayland IMF module for EFL
==========================

An IMF module implemented using the Wayland input method support.

Building
--------

Build requirements:
* eina
* eet
* evas
* ecore

as described in [EFL on Wayland](http://wayland.freedesktop.org/efl.html).

ecore-imf-wayland is than build with:

    $ ./autogen.sh --prefix=$WLD
    $ make
    $ make install

Running
-------

To run an EFL application with the wayland input method:

    $ ECORE_IMF_MODULE=wayland
    $ export ECORE_IMF_MODULE
    $ ./share/elementary/examples/entry_example
