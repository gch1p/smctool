# smctool

A Linux userspace tool to read Apple SMC keys. Supports various data types and output formats.
This tool is a part of the coreboot project.

### Usage

##### Main options

```
-h, --help:         print help
-k, --key <name>:   key name
-t, --type <type>:  data type, see below
```

##### Output format options:
```
--output-hex:  print value as a hexadecimal number
--output-bin:  print binary representation
```

##### Supported data types
`ui8`, `ui16`, `ui32`, `si8`, `si16`, `flag`, `fpXY`, `spXY`

**fp** and **sp** are unsigned and signed fixed point data types respectively.
The `X` in **fp** and **sp** data types is integer bits count and `Y` is fraction bits count.

For example: `fpe2` means 14 integer bits, 2 fraction bits, `sp78` means 7 integer bits, 8 fraction bits (and one sign bit).

### Examples

Reading battery level:
```
smctool -k B0FC -t ui16  # returns Full Capacity of Battery 0
smctool -k B0RM -t ui16  # returns Remaining Capacity of Batery 0
```

Reading fan speed:
```
smctool -k F0Ac -t fpe2
```

### License

GPLv2
