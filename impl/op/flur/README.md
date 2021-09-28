# Flur library

## Functional lambda 

Flur is intended to give C++ developers build functional lazy evaluated pipeline for data 
processing. Project uses features of c++17 standard and inspired by best practices of
.Net lambda fluent interface and Java streams.
Flur allows developer to create processing pipeline in style of functional programming. 
Developers can use as synchronous as
asynchronous processing.

To build Flur pipeline we need specify some source of information, then we can add 
multiple (or even zero) processing declarations and last some consumption 
declaration.

Flur provides some useful implementations for 
- Source
- Processing
- Consumption.

And of course, developer can provide own implementation.

## Getting started
Let's start from simple 