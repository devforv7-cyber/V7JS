# V7JS
A lightweight embeddable JavaScript forked from ELK
you can compile v7.c but if you want you can compile v7.h aswell
Documentation
Project attribution and independence
V7 is a fork of Cesnata’s ELK JavaScript engine. V7 is independently developed and maintained and is not affiliated with, endorsed by, sponsored by, or otherwise related to Cesnata or any of Cesnata’s products.

V7JS is a compact JavaScript engine written in C. It is designed to be embedded in applications and built for different environments, including Linux, Windows, and WebAssembly.

This documentation introduces V7JS, explains how to build and use the runtime, and describes the JavaScript features it supports. It also provides platform-specific guides, code examples, and links to the source code and release information.

About V7JS
V7JS parses and executes JavaScript source code inside a small C runtime. An application can embed V7JS, evaluate scripts, and connect JavaScript code to functionality provided by the host application.

Documentation overview
Getting started
Introduction and project overview
Building and running V7JS
What's new in V7 4.12.0?
JavaScript language support
Supported syntax and built-ins
JavaScript examples
Tests and validation
Building V7JS from source
Building on Linux
Building on Windows
Building with Emscripten and WebAssembly
Using other C compilers
Embedding and development
V7JS C source and API declarations
Build and test targets
Build helper script
Using V7JS
To get started, download the source and follow the build guide for your platform. The repository includes a build script and Makefile for compiling the runtime and running its tests.

Linux build guide
Emscripten / WebAssembly guide
Windows build guide
Release information
The current published release is V7JS 4.12.0. See the release notes, validation report, or the version metadata endpoint for details.
