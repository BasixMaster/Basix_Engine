(()=>{'use strict';
const key='chat-docs.theme.v1',root=document.documentElement;
// One controller and one native picker even when the shared script is included twice.
if(root.dataset.docThemeControllerMounted)return;
root.dataset.docThemeControllerMounted='true';
const values=['system','light','dark'];
let mq={matches:false};try{if(typeof matchMedia==='function')mq=(()=>{try{if(typeof matchMedia==='function')return matchMedia('(prefers-color-scheme: dark)');}catch{}return {matches:false};})();}catch{}
let choice='system';
try{const saved=localStorage.getItem(key);if(values.includes(saved))choice=saved;}catch{}
const apply=()=>{root.dataset.docTheme=choice==='system'?(mq.matches?'dark':'light'):choice;};
apply();
if(mq.addEventListener)mq.addEventListener('change',apply);else if(mq.addListener)mq.addListener(apply);
const mount=()=>{
 const header=document.querySelector('.doc-header');
 const host=header||document.querySelector('#reader-navigation,#study-hub-nav,.topbar,.topnav,nav.study-nav')||document.querySelector('header');
 if(!host)return;
 // Existing Display disclosures retain their other controls; the picker stays visible.
 const settings=header?.querySelector('.doc-header-settings');
 let label=document.querySelector('[data-doc-theme-control]');
 let select=label?.querySelector('select');
 if(!select)select=[...document.querySelectorAll('select')].find(node=>values.every(value=>[...node.options].some(option=>option.value===value)));
 if(select&&!label)label=select.closest('label');
 if(!label){
  label=document.createElement('label');label.className='doc-theme-control';
  label.innerHTML='<svg class="doc-control-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8"/><path d="M12 4v16M12 4a8 8 0 0 1 0 16Z"/></svg><span>Theme</span>';
 }
 label.classList.add('doc-theme-control');label.dataset.docThemeControl='';
 if(!select){
  select=document.createElement('select');
  for(const value of values){const option=document.createElement('option');option.value=value;option.textContent=value[0].toUpperCase()+value.slice(1);select.append(option);}
 }
 select.setAttribute('aria-label','Color theme');select.value=choice;
 if(!label.contains(select))label.append(select);
 select.addEventListener('change',()=>{
  if(!values.includes(select.value))return;
  choice=select.value;try{localStorage.setItem(key,choice);}catch{}apply();
 });
 if(settings){let before=settings;while(before.parentNode&&before.parentNode!==header)before=before.parentNode;if(before.parentNode===header)header.insertBefore(label,before);else host.append(label);}else host.append(label);
 if(!document.querySelector('input[type="search"]')){
  const searchHost=document.querySelector('[data-doc-theme-host]')||host;
  const wrap=document.createElement('label');wrap.className='doc-local-search';wrap.textContent='Search this page ';
  const field=document.createElement('input');field.type='search';field.setAttribute('aria-label','Search this page');
  const results=document.createElement('div');results.className='doc-local-results';results.hidden=true;results.setAttribute('role','status');
  const main=document.querySelector('main')||document.body;
  const records=[...main.querySelectorAll('section[id]')].map(x=>({id:x.id,title:x.querySelector('h2,h3')?.textContent||x.id,text:x.textContent.toLowerCase()}));
  field.addEventListener('input',()=>{
   const terms=field.value.toLowerCase().trim().split(/\s+/).filter(Boolean);results.replaceChildren();results.hidden=!terms.length;if(!terms.length)return;
   const hits=records.filter(x=>terms.every(t=>x.text.includes(t)));const note=document.createElement('p');note.textContent=hits.length+' matching sections on this page';results.append(note);
   for(const x of hits){const link=document.createElement('a');link.href='#'+x.id;link.textContent=x.title;results.append(link);}
  });
  wrap.append(field);searchHost.append(wrap);main.prepend(results);
 }
};
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount,{once:true});else mount();
})();
/* Responsive order and compact navigation; move original nodes, never duplicate IDs. */
(()=>{'use strict';
const mount=()=>{
 if(document.documentElement.dataset.docResponsiveMounted)return;
 document.documentElement.dataset.docResponsiveMounted='true';
 const mq=(()=>{try{if(typeof window.matchMedia==='function'){const value=window.matchMedia('(max-width: 1000px)');if(value)return value;}}catch{}return {get matches(){return window.innerWidth<=1000;},addEventListener(_type,listener){window.addEventListener('resize',listener);},addListener(listener){window.addEventListener('resize',listener);}};})();
 const research=document.querySelector('[data-research-template-layout]');
 const shell=document.querySelector('.doc-layout:not([data-research-template-layout])');
 const book=document.querySelector('.book-sidebar');
 const entries=[];
 const register=(node,rail,language)=>{
  if(!node||!rail)return;
  const marker=document.createComment('responsive-banner-home');
  node.parentNode.insertBefore(marker,node);
  entries.push({node,rail,marker,language});
 };
 if(research)register(research.querySelector('.doc-content>header[data-topic-banner]'),research.querySelector('#research-section-sidebar'));
 if(shell)register(shell.querySelector('.doc-main>.doc-banner'),shell.querySelector('.doc-sidebar'));
 if(book)document.querySelectorAll('.book-content .pcb-banner,.book-content .pcb-banner-compact').forEach(node=>{
  register(node,book,node.closest('[data-lang]')?.dataset.lang);
 });
 const reorder=()=>{
  for(const item of entries){
   const {node,rail,marker,language}=item;
   const focus=document.activeElement;const keepFocus=focus&&node.contains(focus);
   if(mq.matches){
    if(language){node.dataset.lang=language;node.lang=language;node.hidden=language!==document.documentElement.lang;}
    node.classList.add('doc-mobile-banner');
    rail.parentNode.insertBefore(node,rail);
   }else{
    marker.parentNode.insertBefore(node,marker.nextSibling);
    node.classList.remove('doc-mobile-banner');
    if(language){delete node.dataset.lang;node.removeAttribute('lang');node.hidden=false;}
   }
   if(keepFocus&&focus.focus)focus.focus({preventScroll:true});
  }
 };
 // Labels get their own flexible column; numbered prefixes/icons never consume label width twice.
 document.querySelectorAll('.doc-sidebar nav a,#research-section-sidebar nav a,.book-sidebar nav a').forEach(link=>{
  if(link.querySelector('.doc-nav-label,.doc-link-label'))return;
  const label=document.createElement('span');label.className='doc-link-label';
  [...link.childNodes].forEach(child=>{
   if(child.nodeType===1&&child.matches('svg,.doc-section-number,.chapter-order,.doc-nav-number'))return;
   label.append(child);
  });
  if(label.textContent.trim())link.append(label);
 });
 reorder();
 if(mq.addEventListener)mq.addEventListener('change',reorder);else mq.addListener(reorder);
};
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount,{once:true});else mount();
})();
