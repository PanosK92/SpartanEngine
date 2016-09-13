float GetDepth(float2 texCoord)
{
	return texDepth.Sample(samplerAniso, texCoord).r;
}
float SSAO(float2 texCoord)
{
	return GetDepth(texCoord);
}