# Tsduck plugin SpliceManipulator

Tsduck SpliceManipulator plugin processes SCTE-35 messages on a specified splice PID.

 It can
filter messages by `command_type` and remap the `segmentation_type_id` in
TimeSignal segmentation descriptors. 

Only TimeSignal
messages with exactly one descriptor are supported.

## Usage

Use `--splice-pid` to select the SCTE-35 PID and `--rules` to load the JSON
rules file:

```sh
tsp -I <input> -P splicemanipulator --splice-pid 96 --rules rules.json -O drop
```

### Rules JSON Schema

The rule json file supports `manipulate` and `filter`
operations

- `manipulate`: translate a TimeSignal segmentation_type_id `in` into `out`.
- `filter`: filters all the SCTE-35 messages with that `command_type` 

Example:  [./rules.json](.rules.json)
```json
{
	"manipulate": [
		{ "in": 48, "out": 34 },
		{ "in": 49, "out": 35 }
	],
	"filter": [ 
		{ "command": 4 },
		{ "command": 5 }
	]
}
```
Which it means:
- Translate the TimeSignals `segmentation_type_id` from `AdvertisementProviderStart/End` to `BreakStart/End`
- Filter all `SpliceInsert` and `SpliceSchedule` SCTE-35 commands.


## Installation
Need privileges for installation
```
make build
make install
```

## Dependencies
- tsduck version `v3.44-4676`
- CMake version `4.4.3`
- CMake generator for Windows `Visual Studio 18 2026`
- CMake generator for Linux `Ninja`