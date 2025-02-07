# Converts BLP textures to images

This tool converts images in BLP texture file format used in many games such as
World of Warcraft into PNG files.

It works with DXT1, DXT3 and DXT5 compressions so it should convert anything
from WoW.

> [!NOTE]
> It should work on all operating systems. There is no funky code in this
> repository or or any dependencies you need to have installed on your machine.
> C compiler and Libc is all you need.

## Compile & Use

```sh
make -B
```

This will create `blpconvert` binary. Check available options with
`./blpconvert -h`.

Basic example of usage is `./blpconvert samples/Ability_Ambush.blp`.

You can provide multiple input files or you can also use Bash expansion when
providing files to the tool.

```sh
./blpconvert samples/*.blp
```

Program supports exporting to the following formats:

- PNG
- BMP
- JPG
- TGA

```sh
./blpconvert -f bmp samples/Ability_Ambush.blp
```

> [!IMPORTANT]
> By default it will use PNG but you can see use `-f` flag.

## Verbose output

If you provide `-v` flag the program will output a bunch of diagnostical data.

```sh
$ ./blpconvert samples/Ability_Ambush.blp -v
Processing File:
  Fullname: samples/Ability_Ambush.blp
  Folder: samples
  Filename: Ability_Ambush
  Extension: .blp
  Format: png
BLP File Details:
  Type: 1, BLP/DXTC/Uncompressed
  Compression: 2, DXTC
  Alpha Depth: 8
  Alpha Type: 1
  Has Mipmaps: 17
  Width: 64, Height: 64
Reading image data at offset 1172, size 4096 bytes
BLP is compressed with DXTC.
Image has 4096 bytes.
Saving decoded image as PNG...
Successfully saved samples/Ability_Ambush.png

First few pixels of decoded image (RGBA format):
(  0,  0,  0,  0) (  0,  0,  0,  0) (  0,  0,  0,  0) (  0,  0,  0,  0)
(  0,  0,  0,  0) (  0,  0,  0,  0) (  0,  0,  0,  0) (  0,  0,  0,  0)
(  0,  0,  0,  0) (  0,  0,  0,  0) (  0,  0,  0, 51) (184,200,200,221)
(  0,  0,  0,  0) (  0,  0,  0,  0) (184,200,200,221) (184,200,200,255)
```

## Reading material

- https://wowwiki-archive.fandom.com/wiki/BLP_file
- https://en.wikipedia.org/wiki/S3_Texture_Compression
- https://wowdev.wiki/BLP

## Special thanks

- https://github.com/nothings/stb

## License

[blpconvert](https://github.com/mitjafelicijan/blpconvert) was written by [Mitja
Felicijan](https://mitjafelicijan.com) and is released under the BSD
two-clause license, see the LICENSE file for more information.
