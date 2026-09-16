// Phase-0 DXIL acceptance test.  Intentionally has no resources and is never
// dispatched: successful cs_6_5 compilation plus PSO creation proves the DXC
// artifact, engine root signature and D3D12 driver all agree on DXIL.
[numthreads(1, 1, 1)]
void main()
{
}
