# Plot3D shaders

`plot3d.vert` and `plot3d.frag` are the GLSL sources for the 3D peak-map canvas.
The checked-in `.qsb` files are their pre-baked forms carrying GLSL, HLSL and MSL
variants, so building this package needs no shader tooling; QRhi picks the variant
its backend wants at run time.

Regenerate both after editing a source, with the `qsb` tool of the same Qt the
package is built against:

```sh
qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o plot3d.vert.qsb plot3d.vert
qsb --glsl "100 es,120,150" --hlsl 50 --msl 12 -o plot3d.frag.qsb plot3d.frag
```
