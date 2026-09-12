# DXMT

A Metal-based translation layer for Direct3D 11 and 10 which allows running 3D applications on macOS using Wine.

For the current status of the project, please refer to the [project wiki](https://github.com/3Shain/dxmt/wiki).

The most recent development builds can be found [here](https://github.com/3Shain/dxmt/actions).


## Build

See [DEVELOPMENT.md](docs/DEVELOPMENT.md)

## Apple Metal Shader Converter

DXMT optionally uses Apple's Metal Shader Converter (MSC) runtime to convert DXIL shaders. The public MSC headers required by the runtime bridge are included in [`include/metal_shader_converter`](include/metal_shader_converter); the MSC runtime itself is loaded dynamically and is not bundled.

These headers are Copyright 2023-2025 Apple Inc. and are distributed under the Apache License 2.0. See [`include/metal_shader_converter/LICENSE.txt`](include/metal_shader_converter/LICENSE.txt).
