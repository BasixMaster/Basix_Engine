/* Current rendered documentation only. No fetch, telemetry or HTML snippets. */
(()=>{'use strict';
function additionalIndex(data,href,kind){
 const locationURL=new URL(kind==='topic'?'../_doc-search/additive-index.js':'additive-index.js',href).href;
 if(!window.__codexAdditiveHelperPromise)window.__codexAdditiveHelperPromise=new Promise((resolve,reject)=>{if(window.CodexIndexAdditions){resolve();return}const s=document.createElement('script');s.src=locationURL;s.async=true;const timer=setTimeout(()=>fail(Error('Additional index loader timed out')),10000);function fail(e){clearTimeout(timer);s.remove();window.__codexAdditiveHelperPromise=null;reject(e)}s.onload=()=>{clearTimeout(timer);s.remove();window.CodexIndexAdditions?resolve():fail(Error('Additional index loader invalid'))};s.onerror=()=>fail(Error('Additional index loader unavailable'));document.head.appendChild(s)});
 return window.__codexAdditiveHelperPromise.then(()=>window.CodexIndexAdditions.load(data,href,kind)).catch(e=>({...data,additions_status:'unavailable',additions_error:e.message}));
}

 const owner=document.currentScript||document.querySelector('script[data-full-doc-search]');if(!owner||document.querySelector('[data-full-doc-control]'))return;
 const base=new URL(owner.dataset.base,location.href), indexURL=new URL(owner.dataset.index,location.href);
 const style=document.createElement('style');style.textContent=`
 html body [data-full-doc-control]{position:relative;display:grid!important;grid-template-columns:minmax(0,1fr);align-items:center;gap:4px;min-width:min(240px,100%)!important;max-width:380px!important;font:14px/1.4 system-ui,sans-serif;flex:1 1 240px!important}
 html body [data-full-doc-control] input{min-width:0!important;width:100%!important;max-width:100%;box-sizing:border-box;font:inherit;padding:8px;border:1px solid #73899c;border-radius:5px;background:var(--doc-bg,#fff);color:var(--doc-text,#17344a)}
 #full-doc-results{position:fixed;z-index:2200;top:var(--full-doc-top,100px);right:16px;width:min(620px,calc(100vw - 32px));max-height:65dvh;overflow:auto;background:#f8fbff;color:#183248;border:1px solid #718ba2;border-radius:9px;padding:16px;box-shadow:0 8px 30px #0004;font:14px/1.5 system-ui,sans-serif;text-align:left}
 #full-doc-results[hidden]{display:none!important}#full-doc-results a{display:block;color:#145a89;margin:12px 0 2px;overflow-wrap:anywhere}#full-doc-results p{margin:4px 0 10px}#full-doc-results button{font:inherit;min-height:36px;cursor:pointer}#full-doc-results :focus-visible{outline:3px solid #d18b00;outline-offset:3px}
 :root[data-doc-theme=dark] #full-doc-results,:root[data-theme=dark] #full-doc-results,html[data-color-mode=dark] #full-doc-results{background:#172c3d;color:#edf5ff}:root[data-doc-theme=dark] #full-doc-results a,:root[data-theme=dark] #full-doc-results a,html[data-color-mode=dark] #full-doc-results a{color:#a8d7ff}
 @media(prefers-color-scheme:dark){:root:not([data-theme=light]):not([data-doc-theme=light]):not([data-color-mode=light]) #full-doc-results{background:#172c3d;color:#edf5ff}:root:not([data-theme=light]):not([data-doc-theme=light]):not([data-color-mode=light]) #full-doc-results a{color:#a8d7ff}}
 @media(max-width:650px){[data-full-doc-control]{width:100%}#full-doc-results{right:8px;width:calc(100vw - 16px);box-sizing:border-box;max-height:56dvh}}
 @media print{#full-doc-results,[data-full-doc-control]{display:none!important}}`;
 document.head.append(style);
 const header=document.querySelector('.doc-header,#reader-navigation,nav.topbar,header .topbar,header');
 let input=header?.querySelector('input[type=search]'),control,preservedDiscovery=false;
 if(input&&/\b(?:hub-cards|research-topic-cards)\b/.test(input.getAttribute('aria-controls')||'')){
   // This native field also filters hub cards. Keep that behavior in content,
   // and replace its former header position with the documentation control.
   const local=input.closest('label')||input.parentElement,main=document.querySelector('main');
   if(main){const replacement=document.createElement('label');replacement.className='doc-search';header.insertBefore(replacement,local);main.prepend(local);local.style.cssText='margin:12px 0;max-width:100%;display:flex;gap:8px;flex-wrap:wrap';for(const node of [...local.childNodes])if(node!==input)node.remove();local.prepend(document.createTextNode('Filter this hub '));input.setAttribute('aria-label','Filter this hub');const field=document.createElement('input');field.type='search';replacement.append(field);input=field;preservedDiscovery=true;}
 }
 if(input){
   control=input.closest('label,.anchored-search,.rs-controls,.doc-search')||input.parentElement;
   control.dataset.fullDocControl='';
   const label=control.tagName==='LABEL'?control:control.querySelector('label');
   if(label){for(const node of [...label.childNodes])if(node!==input)node.remove();if(label.contains(input))label.prepend(document.createTextNode('Search all documentation '));else label.textContent='Search all documentation';}
   // Retain old IDs/data attributes: native shell initialization and unrelated navigation remain valid.
 }else{
   control=document.createElement('label');control.dataset.fullDocControl='';control.append(document.createTextNode('Search all documentation '));input=document.createElement('input');input.type='search';control.append(input);
   if(header)header.append(control);else{const nav=document.createElement('nav');nav.setAttribute('aria-label','Documentation search');nav.style.cssText='padding:10px 16px;position:relative;z-index:20';nav.append(control);document.body.prepend(nav);}
 }
 control.querySelectorAll('a[href="#book-search-panel"],a[href="#research-keywords"]').forEach(a=>a.remove());
 input.setAttribute('aria-label','Search all documentation');input.placeholder='Pages, sections and appendices';input.maxLength=200;input.autocomplete='off';input.setAttribute('aria-controls','full-doc-results');
 // A language/settings callback may call the legacy renderer directly. Retire only
 // its result panel; discovery cards/tables and product filters remain untouched.
 if(header?.contains(input)&&!preservedDiscovery)document.querySelectorAll('#book-search-panel,.doc-results,#research-keywords').forEach(p=>{p.hidden=true;p.setAttribute('data-full-doc-retired','');});
 style.textContent+='[data-full-doc-retired]{display:none!important}';
 const panel=document.createElement('section');panel.id='full-doc-results';panel.hidden=true;panel.setAttribute('aria-label','Documentation search results');panel.setAttribute('tabindex','-1');document.body.append(panel);
 let pending,loaded,revision=0,timer;
 function load(){if(loaded)return Promise.resolve(loaded);if(pending)return pending;pending=new Promise((resolve,reject)=>{const s=document.createElement('script');s.src=indexURL.href;s.onload=()=>{const data=window.__FULL_DOC_INDEX__;if(!data?.records){pending=null;reject(Error('Invalid documentation index'));return}additionalIndex(data,indexURL.href,'header').then(result=>{if(result.additions_status==='unavailable'){loaded=null;pending=null}else loaded=result;resolve(result)});};s.onerror=()=>{s.remove();pending=null;reject(Error('Documentation index unavailable. Keep the _doc-search folder with this documentation.'));};document.head.append(s);});return pending;}
 const norm=s=>s.toLocaleLowerCase().normalize('NFKD').replace(/[\u0300-\u036f]/g,'');
 function lang(){return document.documentElement.lang.slice(0,2)||'en';}
 function message(text){const p=document.createElement('p');p.textContent=text;p.setAttribute('role','status');panel.append(p);}
 function close(){panel.hidden=true;input.setAttribute('aria-expanded','false');}
 async function search(){const run=++revision,query=input.value.trim();panel.replaceChildren();if(query.length<2){close();return;}panel.hidden=false;input.setAttribute('aria-expanded','true');const bounds=control.getBoundingClientRect();document.documentElement.style.setProperty('--full-doc-top',Math.min(bounds.bottom+8,innerHeight*.4)+'px');message('Loading documentation index…');
  try{const data=await load();if(run!==revision)return;const terms=norm(query).split(/\s+/).filter(Boolean);const matches=[];const seen=new Set();for(const r of data.records){if(r.lang&&r.lang.slice(0,2)!==lang())continue;const hay=norm(r.title+' '+r.page+' '+r.text);if(terms.every(t=>hay.includes(t))){const key=r.path+'#'+r.anchor;if(!seen.has(key)){seen.add(key);matches.push(r);}}}panel.replaceChildren();message(`${data.additions_status==='unavailable'?'Additional collections unavailable; base index only. ':''}${matches.length} matching sections across ${data.pages} documentation pages${matches.length>40?' · first 40 shown; refine your query':''}.`);
   for(const r of matches.slice(0,40)){const a=document.createElement('a');const url=new URL(r.path,base);if(r.anchor)url.hash=r.anchor;if(data.scope==='electronics-cookbook')url.searchParams.set('lang',lang());a.href=url.href;a.textContent=r.title===r.page?r.title:r.page+' · '+r.title;panel.append(a);const snippet=document.createElement('p');const at=Math.max(0,norm(r.text).indexOf(terms[0])-65);snippet.textContent=(at?'…':'')+r.text.slice(at,at+210)+(r.text.length>at+210?'…':'');panel.append(snippet);}
  }catch(e){if(run===revision){panel.replaceChildren();message(e.message);}}
  const closeButton=document.createElement('button');closeButton.type='button';closeButton.textContent='Close results';closeButton.onclick=close;panel.append(closeButton);
 }
 // Capture prevents legacy page-filter handlers from consuming the upgraded header input.
 // Capture suppresses native filter listeners, so keep its existing Clear UI
 // synchronized here rather than relying on a suppressed bubble listener.
 const nativeClear=header?.querySelector('#docSearchClear');
 const syncNativeClear=()=>{if(nativeClear){const empty=input.value.length===0;nativeClear.hidden=empty;nativeClear.disabled=empty;}};
 syncNativeClear();
 input.addEventListener('input',e=>{syncNativeClear();e.stopImmediatePropagation();clearTimeout(timer);timer=setTimeout(search,130);},true);
 input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();e.stopImmediatePropagation();clearTimeout(timer);search().then(()=>panel.querySelector('a,button')?.focus());}else if(e.key==='Escape'){e.preventDefault();e.stopImmediatePropagation();close();input.focus();}},true);
 panel.addEventListener('keydown',e=>{if(e.key==='Escape'){close();input.focus();}});
 document.addEventListener('click',e=>{if(!control.contains(e.target)&&!panel.contains(e.target))close();});
 new MutationObserver(()=>{if(input.value.trim().length>1&&!panel.hidden)search();}).observe(document.documentElement,{attributes:true,attributeFilter:['lang']});
})();
