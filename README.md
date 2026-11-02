# ACL

**ACL Compiler Language** is a hobby programming language. The name is recursive: the goal is to write ACL's own compiler in ACL itself.

The current compiler is written in C++ and emits C, which is then compiled with Clang.

## Requirements

- Linux
- Clang with C++17 support (also used to compile the generated C)
- Make
- [fmt](https://fmt.dev/11.1/get-started/#installation)
- [GMP](https://gmplib.org/) (with the C++ bindings, `gmpxx`)

## Building

```sh
git clone https://github.com/alex2449/ACL.git
cd ACL/src
make && sudo make install
```

## Usage

```sh
aclc <file.acl>
```

This produces the executable `a.out`. To generate C instead, run:

```sh
aclc --emit-c <file.acl>
```

## Example

```acl
function main(args [*]str) returns (int32) {
    print("Hello World");
    return 0;
}
```

## Status

This compiler and language are a work in progress and very early in development.
