# haliumqsgcontext

A Qt Scene Graph plugin for better hardware integration


## General info

This plugin allows for tighter integration of Qt and Halium-based distributions.

- Uses Android's GraphicBuffer to upload textures to the GPU
- Using multiple threads
- Exposes those GraphicBuffers to OpenGL as any other texture
- Replaces the default QAnimationDriver with a solely VSync-based one

## Usage

Enable the plugin by exporting `QMLSCENE_DEVICE=haliumqsgcontext` in the environment.

## Tweaking the settings

There are a handful of libdeviceinfo settings that allow adjusting the behavior of haliumqsgcontext.

- `HaliumQsgUseShaders` (default: true): Determines shader-based swizzling, falling back to Qt for texture uploads
- `HaliumQsgAnimationDriver` (default: true): Enable or disable the custom QAnimationDriver
- `HaliumQsgUploadThreads` (default: number of cores - 2): Raise or lower thread count for texture uploads, or disable with `0`.

For example:

```
# cat /etc/deviceinfo/devices/halium.yaml
sargo:
  Vendor: Google
  PrettyName: Pixel 3a
  DeviceType: phone
  GridUnit: 25
  HaliumQsgUseShaders: true
  HaliumQsgUploadThreads: 4
```
