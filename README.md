# YAJL-Sparkling

## Sparkling bindings for a fast JSON parser

This repo is both a piece of example code for writing and building
dynamically loadable Sparkling modules, **and** a practically usable
JSON parser and serializer library (something that a lot of you
probably wanted for a long time).

Requires `yajl 2.x` to be installed on your system in advance.

Build using

    make

Copy the library to an appropriate directory (one that the dynamic linker
searches when looking for dynamic libraries) and load it into REPL using,
for example:

	 extern YAJL = dynld("yajl_spn");

Usage:

    YAJL["parse"](theJSONString [, configOpts])
    YAJL["generate"](someSparklingValue [, configOpts])

where `configOpts` is a hashmap containing the following keys and values:

## Parsing options

* `comment`: enable embedding C-style comments into the JSON string
[`true`/`false`].

* `parse_null`: when the JSON value `null` is found, emit the special
`YAJL["null"]` value (which is of type user info) if `true`. If `false`,
then `null` will be turned into `nil` (i. e. keys with a null value won't
appear in the output at all).

## Serialization options

* `beautify`: when `true`, generate human-readable JSON. Else, generate
minified JSON.

* `indent`: a string which is used as one level of indentation when
generating beautified JSON.

* `escape_slash`: when `true`, escape forward slashes (useful for working
with HTML).

Enjoy!

-- H2CO3
