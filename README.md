# haliumqsgcontext

A Qt Scene Graph plugin for better hardware integration


## General info

This plugin allows for tighter integration of Qt and Halium-based distributions.

- Uses Android's GraphicBuffer to upload textures to the GPU, using multiple threads
- Exposes those GraphicBuffers to OpenGL as any other texture
- Replaces the default QAnimationDriver with a solely VSync-based one

## Usage

Enable the plugin by exporting `QMLSCENE_DEVICE=haliumqsgcontext` in the environment.

## Tweaking the settings

There are a handful of libdeviceinfo settings that allow adjusting the behavior of haliumqsgcontext.

- `HaliumQsgUseShaders` (default: true): Determines shader-based swizzling, falling back to Qt for texture uploads
- `HaliumQsgAnimationDriver` (default: true): Enable or disable the custom QAnimationDriver
