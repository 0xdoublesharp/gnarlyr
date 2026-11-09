#!/usr/bin/env node
// Persistent, local REA MCP client for the firmware investigation.
// Dependencies are resolved from the isolated REA runtime, not globally installed.
import {createRequire} from 'node:module';
import {createInterface} from 'node:readline';
import {mkdir,writeFile,access} from 'node:fs/promises';
import {join} from 'node:path';
const runtime='/Volumes/Scratch/rea/build/snowsky-echo/runtime/node_modules/rea-agents';
const require=createRequire(join(runtime,'package.json'));
const {Client}=require('@modelcontextprotocol/client');
const {StdioClientTransport}=require('@modelcontextprotocol/client/stdio');
const output=process.argv[2];
if(!output || !output.startsWith('/Volumes/Scratch/')) throw new Error('Supply an absolute Scratch report directory');
await access('/Volumes/Scratch/rea/t');
await mkdir(output,{recursive:true});
const transport=new StdioClientTransport({command:process.execPath,args:[join(runtime,'scripts/rea.mjs'),'mcp'],env:{
  ...process.env,GHIDRA_INSTALL_DIR:'/Volumes/Scratch/SnowskyEcho/tools/ghidra_12.1.4_PUBLIC',
  JAVA_HOME:'/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home',
  REA_GHIDRA_LANGUAGE_ID:'ARM:LE:32:Cortex',
  REA_GHIDRA_SEED_FILE:process.env.REA_GHIDRA_SEED_FILE??'/Volumes/Scratch/SnowskyEcho/build/initial-investigation/rea-seeds.tsv',
  REA_MCP_OUTPUT_SCHEMA_PROFILE:'off',TMPDIR:'/Volumes/Scratch/rea/t',
  JAVA_TOOL_OPTIONS:'-Duser.home=/Volumes/Scratch/SnowskyEcho/tmp/initial-investigation/java-home -Djava.io.tmpdir=/Volumes/Scratch/rea/t -XX:ActiveProcessorCount=4'
},stderr:'pipe'});
let stderr='';
transport.stderr?.on('data',chunk=>{stderr=(stderr+chunk.toString()).slice(-65536)});
const client=new Client({name:'snowsky-firmware-investigation',version:'1'});
let count=0;
try{
  await client.connect(transport);
  const catalog=(await client.listTools()).tools;
  console.log(JSON.stringify({ready:true,tools:catalog.filter(t=>/binary|pseudo|procedure|references|bytes|memory|instruction/.test(t.name)).map(t=>({name:t.name,description:t.description.slice(0,110)}))}));
  for await(const line of createInterface({input:process.stdin})){
    if(!line.trim())continue;
    const command=JSON.parse(line);
    if(command.close)break;
    if(command.schema){console.log(JSON.stringify(catalog.find(t=>t.name===command.schema)));continue;}
    const result=await client.callTool({name:command.name,arguments:command.arguments??{}},undefined,{timeout:600000});
    const path=join(output,String(++count).padStart(3,'0')+'-'+command.name+'.json');
    await writeFile(path,JSON.stringify({request:command,result},null,2)+'\n');
    const evidence=result.structuredContent;
    const summary=evidence && Object.hasOwn(evidence,'normalized_result')
      ? {evidence_id:evidence.evidence_id,operation:evidence.operation,normalized_result:evidence.normalized_result}
      : evidence??result.content;
    console.log(JSON.stringify({path,isError:result.isError??false,result:summary}));
  }
}finally{
  await client.close();
  await writeFile(join(output,'session-stderr.log'),stderr);
  process.stdin.pause();
  process.stdin.unref?.();
}
