# Quickstart

> [!NOTE]
> This section covers how to quickly test your syntax and run the Virtual Machine via the CLI.

You can instantly evaluate bytecode or scripts using the provided command line utilities.

## Check (Syntax & Symbol Validation)
Compile the source to check for syntax errors before running:
```bash
zephyr check my_script.zph
```

## Run (Execution)
Invoke the standalone `ZephyrVM` tool to interpret scripts directly:
```bash
zephyr run examples/state_machine.zph
```

## Engine Embedding
To natively call functions or bind your own C++ pointers into the `ZephyrVM` context, you must connect via the `api.hpp` interface. Detailed setup information is provided in the [Embedding ZephyrVM](../advanced/embedding) section.
