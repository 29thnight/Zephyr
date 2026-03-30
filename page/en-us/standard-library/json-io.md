# JSON & I/O

A collection of input/output utilities for serializing and parsing external JSON data from files or networks, and for safely reading/writing directly to disk.

## JSON Parsing & Stringifying (`std/json`)

Converts JSON files or strings into native Zephyr data structures (Dictionaries and Arrays), or vice versa. For error safety, the result is returned as a `Result<T>` Enum binding.

```zephyr
import "std/json";

// parse returns a Result<any>
let obj: any = parse("{\"x\": 1, \"y\": 2}");

match obj {
    Ok(v)    => print(stringify(v)),
    Err(msg) => print(f"Parse error: {msg}"),
}
```

| Function | Signature |
|---|---|
| `parse(s)` | `string -> Result<any>` |
| `stringify(v)` | `any -> string` |

## File System Access (`std/io`)

Supports writing to and reading from text files. `read_lines` easily allows line-by-line sequential iteration. To prevent fatal system I/O errors, this module also employs the `Result<T>` pattern.

```zephyr
import "std/io";

// Read the entire contents of a text file
let content = read_file("data.txt");
match content {
    Ok(text) => print(text),
    Err(e)   => print(f"Load failed: {e}"),
}

// Write to a text file
write_file("out.txt", "Hello!\n");

// Read and iterate file line by line
let lines = read_lines("data.txt");
match lines {
    Ok(ls) => {
        // Upon success, iterate through the valid ls group (Iterator)
        for line in ls { print(line); }
    },
    Err(e) => print(e),
}
```
