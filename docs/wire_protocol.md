# Jellybean Wire Protocol (TCP Demo)

## Request Frame
- `uint32 input_elems`
- `float32[input_elems] payload`

All fields are sent in little-endian byte order.

## Response Frame
- `uint8 ok`
- `uint64 latency_ns`
- `uint32 output_elems`
- `float32[output_elems] payload` (only when `ok=1` and `output_elems>0`)

## Validation Rules
- `input_elems` must match `product(input_shape)` from config.
- output size must match `expected_output_elems` from config.
- invalid request or invalid output returns `ok=0`.

## Notes
- This is intentionally minimal for benchmark clarity.
- Next revision can include versioned headers and request ids.
