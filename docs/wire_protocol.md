# Wire Protocol (TCP)

## Request Frame
- `uint32 input_elems`
- `float32[input_elems] payload`

All fields are little-endian.

## Response Frame
- `uint8 ok`
- `uint64 latency_ns`
- `uint32 output_elems`
- `float32[output_elems] payload` (only when `ok=1`)

## Validation
- `input_elems == product(input_shape)`
- output count matches `expected_output_elems`

## Error Behavior
If validation or inference fails, server returns:
- `ok=0`
- `output_elems=0`
