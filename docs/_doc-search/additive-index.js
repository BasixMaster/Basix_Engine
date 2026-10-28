/* Optional, append-only search collections. Existing index records stay intact. */
(()=>{'use strict';
const w=window,loads=new Map(),augmentations=new Map();
function script(url){if(loads.has(url))return loads.get(url);const p=new Promise((resolve,reject)=>{const s=document.createElement('script');let timer;const done=(error)=>{clearTimeout(timer);s.remove();if(error){loads.delete(url);reject(error)}else resolve()};s.src=url;s.async=true;s.onload=()=>done();s.onerror=()=>done(Error('Additional collection script unavailable'));timer=setTimeout(()=>done(Error('Additional collection script timed out')),10000);document.head.appendChild(s)});loads.set(url,p);return p}
function stable(v){if(Array.isArray(v))return '['+v.map(stable).join(',')+']';if(v&&typeof v==='object')return '{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+stable(v[k])).join(',')+'}';return JSON.stringify(v)}
function safePath(value){if(typeof value!=='string'||!value.length||value!==value.trim())return false;let path=value;try{for(let i=0;i<3;i++){const decoded=decodeURIComponent(path);if(decoded===path)break;path=decoded}if(decodeURIComponent(path)!==path)return false}catch{return false}return path===path.trim()&&!/[\u0000-\u001f\u007f\\?#]/.test(path)&&!/^(?:[a-z][a-z0-9+.-]*:|\/)/i.test(path)&&!path.split('/').includes('..')}
function merge(base,addons,kind){
 if(!Array.isArray(base?.records)||(kind==='header'&&(!Number.isInteger(base.pages)||base.pages<0)))throw Error('Invalid base index');
 const result={...base,records:[...base.records]},key=r=>kind==='header'?[r.path,r.anchor||'',r.lang||''].join('#'):r.key;
 const existing=new Map(base.records.map(r=>[key(r),r])),pages=new Set(base.records.map(r=>r.path)),newPages=new Set();
 if(kind==='topic'){if(!Array.isArray(base.collections))throw Error('Invalid topic catalogue');result.collections=[...base.collections]}
 let added=0;
 for(const addon of addons){if(!addon||!Array.isArray(addon.records)||(kind==='header'?addon.schema!==1:addon.schema_version!==1))throw Error('Invalid additional index');
  for(const r of addon.records){if(!r||!safePath(r.path)||typeof r.anchor!=='string'||typeof r.title!=='string'||typeof r.text!=='string'||(kind==='topic'&&typeof r.key!=='string'))throw Error('Invalid additional record');const id=key(r);if(existing.has(id)){if(stable(existing.get(id))!==stable(r))throw Error('Additional index would overwrite an existing record');continue}existing.set(id,r);result.records.push(r);added++;if(!pages.has(r.path))newPages.add(r.path)}
  if(kind==='topic'){if(!Array.isArray(addon.collections))throw Error('Invalid additional collections');for(const c of addon.collections){if(!c||!safePath(c.directory))throw Error('Invalid collection path');const old=result.collections.find(x=>x.directory===c.directory);if(old){if(stable(old)!==stable(c))throw Error('Additional collection would overwrite metadata')}else result.collections.push(c)}}
 }
 if(kind==='header')result.pages=Number(base.pages)+newPages.size;
 result.additions_status='loaded';result.additions_added_records=added;return result;
}
async function load(base,indexURL,kind){
 if(!['topic','header'].includes(kind))throw Error('Unknown index kind');const absolute=new URL(indexURL,document.baseURI).href,key=kind+'|'+absolute;
 if(augmentations.has(key))return augmentations.get(key);
 const p=(async()=>{try{await script(new URL('collection-addons.js',absolute).href);const names=w[kind==='topic'?'__RESEARCH_SEARCH_ADDON_FILES__':'__FULL_DOC_SEARCH_ADDON_FILES__'];if(!Array.isArray(names)||names.some(n=>typeof n!=='string'||! /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(n)))throw Error('Invalid additional collection registry');for(const n of names)await script(new URL('collection-addons/'+n+'.js',absolute).href);const data=w[kind==='topic'?'__RESEARCH_SEARCH_ADDONS__':'__FULL_DOC_SEARCH_ADDONS__']||{};const merged=merge(base,names.map(n=>data[n]),kind);return merged}catch(error){augmentations.delete(key);return {...base,additions_status:'unavailable',additions_error:error.message}}})();augmentations.set(key,p);return p;
}
w.CodexIndexAdditions={load,merge};
})();
