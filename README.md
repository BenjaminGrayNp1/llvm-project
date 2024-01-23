# Prototype Clangd + Assembly project

This project enables Clangd to intergrate with Assembly files too. It leverages existing components to do the semantic work, just adding glue code to convert data between the different forms used by these components.


## Architecture

Broadly, it:

- Serialises the C preprocessor (CPP) tokens back into a string, keeping track of the offset each token is written to.
- Uses this to build a bi-directional map between `SourceLocation`'s and offsets into the CPP buffer.
- Constructs an Assembly parser based on the compiler invocation.
- Runs the parser on the serialised output of the CPP.
- Attaches the Assembly AST as a field on the C AST. The C AST is made to look as empty as possible so we don't get unwanted C diagnostics.
- At specific points of interest, checks for an Assembly AST and uses it if it exists.

While LLVM supports both the CPP and Assembly, it does not (currently) support combining the two. So a major part of this work is creating that bi-directional mapping between the original source file and CPP output, such that we can translate locations back and forth.

The Assembly parser generates the semantic information we need. Its locations are expressed in terms of `SMLoc` instances, which can be directly converted to offsets into the CPP buffer. We then apply the bi-directional map to retrieve the CPP token corresponding to that offset, and from there the appropriate location to send back to the client.

For operations that start at a user source location we apply the forward mapping to determine what offset in the CPP buffer the source location maps to in the CPP output. We can then act based on what the Assembly AST says this location points to. For example, if we are hovering over an instruction name, we can use the instruction to search for corresponding documentation to display.

The Assembly AST is made a field of the C AST to reduce the impact of this patch. I need to be able to merge upstream changes easily, so I tried to minimise the changes made to existing files. I imagine proper upstream support might do something more integrated (e.g., make the AST a variant over the supported kinds). I'm also not experience in the LLVM codebase, so some things here might have more efficient alternatives / be used incorrectly.


## Features

### Semantic highlighting

Semantic highlighting is provided by capturing the emitted instructions and highlighting the instruction name. More complete syntax highlighting is desirable (see below).


### Hover

CPP macro support is similar to in C files. There is some dedicated Assembly logic for formatting the expansion better.

There is also support for custom documentation on hover. This can be used to provide information about the active instruction, such as it's name, encoding details, pseudocode, description, and where to find more details in the official documentation.


## Future work

Here's some possible work that could be explored if there's interest in the project:

- More semantic highlighting. Registers, constants (PowerPC can write them both as numeric literals, so only a semantic highlighter can distinguish them), directives, labels, strings, etc. The parser currently doesn't emit this information directly, so it will take some work to capture things.
- Support symbol indexing. Uses of C variables/functions, goto label declaration and usage, etc.
- Highlight and expand Assembly macros like how CPP macros are.
- Test `.include` (`.input`?) Assembly directives
- Rewriting CPP tokens. They are currently split weirdly for the assembly, such as breaking `mr.` into `mr` and `.` (the `mr.` form with the dot is like the `s` suffix in ARM, where it writes to the flags). Useful because if the user hovers over the `mr` we want the hover range to include the dot too.

Somewhat separately, this demonstrates another use for https://github.com/clangd/clangd/issues/519. Kernel headers are sometimes shared by C and Assembly code, and have very different contents conditionally compiled in. The workaround right now is to reload the editor/server with a different main file open to update the cache.


--------------------------------------------------------------------------------

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](http://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
