# JSONB container layout

The partial-read path depends on the PostgreSQL JSONB binary layout. This page
explains the parts that `jsonb_slice` reads. It does not describe the complete
JSONB format.

## Two different headers

A PostgreSQL `jsonb` datum has an outer varlena header. The JSONB data starts
after that header. A TOAST slice offset addresses the JSONB data, not the
outer varlena header.

The JSONB data starts with a container header:

```text
outer varlena header
        |
        | not part of the slice offset
        v
slice offset 0
+---------------------------+
| JSONB container header    | 4 bytes
+---------------------------+
| JSONB directory and data  |
+---------------------------+
```

The container header records:

- whether the container is an object, array, or scalar wrapper;
- the number of object pairs or array elements.

`JSONB_CONTAINER_HEADER_SIZE` names this four-byte size in the C source. This
name prevents the code from confusing the container header with the varlena
header.

## Object directory

Consider this object:

```json
{
  "name": "Bilal",
  "role": "developer"
}
```

The object has two key-value pairs. JSONB uses one `JEntry` for each key and
one `JEntry` for each value. Each `JEntry` is four bytes.

```text
logical JSONB data
+----------------------------------+
| container header                 | type = object, count = 2
+----------------------------------+
| key JEntry 0                     | describes "name"
| key JEntry 1                     | describes "role"
| value JEntry 2                   | describes "Bilal"
| value JEntry 3                   | describes "developer"
+----------------------------------+ <- data_start
| name                             | key bytes
| role                             | key bytes
| Bilal                            | value bytes
| developer                        | value bytes
+----------------------------------+
```

The directory does not contain the text `name`, `role`, `Bilal`, or
`developer`. It contains only type and position metadata. Each text value
appears once, in the data area.

`slice_lookup()` therefore makes separate reads for separate purposes:

```text
directory slice -> metadata that locates each key and value
key slice       -> actual key bytes used by memcmp()
value slice     -> actual bytes of the selected value only
```

For an object with `N` pairs:

```text
directory entries = 2 * N
directory bytes   = 2 * N * sizeof(JEntry)
data_start        = JSONB_CONTAINER_HEADER_SIZE + directory bytes
```

All key entries come first. All value entries follow them. The key bytes then
come before the value bytes.

## Array directory

An array uses one `JEntry` for each element. It does not have separate key
entries.

```json
[10, 20, {"name": "Ada"}]
```

```text
+----------------------------------+
| container header                 | type = array, count = 3
+----------------------------------+
| element JEntry 0                 | describes 10
| element JEntry 1                 | describes 20
| element JEntry 2                 | describes the object
+----------------------------------+ <- data_start
| element payloads                 |
+----------------------------------+
```

The requested index selects one entry. A negative index is first converted to
its position from the start of the array. An index outside the array has no
result.

## Nested containers

The complete JSONB value has one outer varlena header. Each object or array
inside that value has its own container header and directory.

```json
{
  "users": [
    {"name": "Ada"}
  ]
}
```

```text
outer varlena header
└── root object container
    └── "users" value: array container
        └── element 0: object container
            └── "name" value: string
```

A container `JEntry` locates the complete nested container. The nested bytes
start with that container's header. `ContainerLocation` records the absolute
start and size of the current container. This prevents a path lookup from
reading outside the selected container.

For `ARRAY['users', '0', 'name']`, the extension performs these steps:

1. find `users` in the root object;
2. enter the selected array container;
3. locate array element `0`;
4. enter the selected object container;
5. find `name` and read its value.

The extension reads container metadata during the first four steps. It does
not read the other element payloads.

## What a JEntry records

A `JEntry` records the value type and position information. The type can
identify a string, number, Boolean, null, array, or object.

The position field contains either a length or an end offset. PostgreSQL
stores a complete end offset at regular intervals. It stores lengths between
those offsets. `entry_offset()` walks back to a complete offset and adds the
intermediate lengths.

The resulting position is relative to the data area that starts at
`data_start`. The extension adds `data_start` to obtain an absolute TOAST
slice offset.

Some values need alignment padding. Numbers and nested containers can start
after padding bytes. `locate_value()` skips that padding before it records the
value location.

Null and Boolean values need no value bytes. Their complete value is in the
`JEntry` type.

## Why compressed input uses the normal path

The directory positions describe the logical, uncompressed JSONB bytes. They
do not identify positions in a compressed byte stream.

```text
logical JSONB:    [header][directory][keys][values]
compressed TOAST: [          compressed bytes          ]
```

Logical offset 200 is not compressed offset 200. The stored external size is
also the compressed size, not the logical JSONB data size. The partial path
therefore rejects compressed input before it creates `SliceSource`.

PostgreSQL's normal path reads and decompresses that value. The partial path
continues only when the external value is uncompressed.

## Read sequence

For an external, uncompressed object lookup, the partial reader reads:

1. the four-byte container header;
2. the complete `JEntry` directory, which contains metadata only;
3. the actual key bytes used for comparison;
4. the selected value bytes.

Every calculated range is checked against the logical data length before a
slice request. Invalid metadata stops the partial parser and selects the
normal PostgreSQL path.

A path lookup repeats the header, directory, and key steps for each visited
container. An array step has no key slice. The last step reads the complete
selected value.
