// Depth-only pass for shadow-map rendering. Paired with PbrModelShaderVS; no
// colour output, the DSV is all that matters. (Opaque casters only for now --
// alpha-tested casters would sample albedo.a and discard here.)
void main()
{
}
