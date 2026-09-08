import fs from 'node:fs';
// Extract the actual renderer pass and RHI pipeline-selection functions. The
// GPU-only shader fixture cannot catch a missing C++ pass/pipeline transition.
function extract(source,signature) {
    const start=source.indexOf(signature);
    if(start<0)throw new Error(`Missing ${signature}`);
    let end=source.indexOf('{',start),depth=1;
    while(depth&&++end<source.length){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
    if(depth)throw new Error(`Unclosed ${signature}`);
    return source.slice(start,end+1);
}
const passSource=fs.readFileSync(process.argv[2]??'source/rendering/Renderer_Passes_Post.cpp','utf8');
const rhi=fs.readFileSync('source/rhi/RHI_CommandList.cpp','utf8');
const signatures=['void RHI_CommandList::begin_pass(', 'void RHI_CommandList::end_pass(',
    'void RHI_CommandList::set_pass(', 'void RHI_CommandList::set_shader(',
    'bool RHI_CommandList::IsPendingPipelineReady()', 'void RHI_CommandList::TryBindPendingPipeline()'];
fs.mkdirSync('binaries/bloom_tests',{recursive:true});
fs.writeFileSync('binaries/bloom_tests/pass_fixture.h',signatures.map(s=>extract(rhi,s)).join('\n')+'\n'+extract(passSource,'void Renderer::Pass_Bloom('));
