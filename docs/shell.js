(()=>{'use strict';const header=document.querySelector('.doc-header');const size=()=>document.documentElement.style.setProperty('--doc-header-height',header.getBoundingClientRect().height+'px');size();if(typeof ResizeObserver==='function')new ResizeObserver(size).observe(header);else addEventListener('resize',size);document.querySelector('[data-doc-back]').addEventListener('click',()=>{let same=false;try{same=new URL(document.referrer).origin===location.origin&&!!document.referrer;}catch{}if(same&&history.length>1)history.back();else location.href='index.html';});const input=document.querySelector('[data-doc-search]'),results=document.querySelector('.doc-results');const records=[...document.querySelectorAll('main section[id]:not(#sources), main article[id]')].map(s=>({id:s.id,title:s.querySelector('h2,h3')?.textContent||s.id,text:s.textContent.toLowerCase()}));input.addEventListener('input',()=>{const terms=input.value.toLowerCase().trim().split(/\s+/).filter(Boolean);results.replaceChildren();results.hidden=!terms.length;if(!terms.length)return;const matches=records.filter(r=>terms.every(t=>r.text.includes(t)));const p=document.createElement('p');p.textContent=matches.length+' matching chapters and sources';results.append(p);for(const r of matches){const a=document.createElement('a');a.href='#'+r.id;a.textContent=r.title;results.append(a);}});
// On-page location is independent from the current document link.
const sectionLinks=[...document.querySelectorAll('.doc-sidebar nav[aria-label="On this page"] a[href^="#"]')];
const sections=sectionLinks.map(a=>document.getElementById(decodeURIComponent(a.hash.slice(1))));
function mark(index){sectionLinks.forEach((a,i)=>{if(i===index)a.setAttribute('aria-current','location');else a.removeAttribute('aria-current');});}
function locationFromScroll(){const edge=header.getBoundingClientRect().bottom+30;let active=0;sections.forEach((s,i)=>{if(s&&!s.hidden&&s.getBoundingClientRect().top<=edge)active=i;});mark(active);}
let scheduled=false;const schedule=()=>{if(scheduled)return;scheduled=true;requestAnimationFrame(()=>{scheduled=false;locationFromScroll();});};
sectionLinks.forEach((a,i)=>a.addEventListener('click',()=>mark(i)));
addEventListener('scroll',schedule,{passive:true});addEventListener('resize',schedule,{passive:true});
addEventListener('hashchange',()=>{const i=sectionLinks.findIndex(a=>a.hash===location.hash);if(i>=0)mark(i);else schedule();});
const initial=sectionLinks.findIndex(a=>a.hash===location.hash);if(initial>=0)mark(initial);else schedule();
const settings=document.querySelector('.doc-header-settings');if(settings)settings.addEventListener('keydown',e=>{if(e.key==='Escape'&&settings.open){settings.open=false;settings.querySelector('summary').focus();}});
})();

// doc-image-viewer:start
// Shared documentation image preview. Native dialog owns focus trapping and Escape.
(() => {
  'use strict';
  if (document.documentElement.hasAttribute('data-doc-image-viewer-ready')) return;
  if (typeof HTMLDialogElement === 'undefined') return; // Retain ordinary image links.
  document.documentElement.setAttribute('data-doc-image-viewer-ready', '');
  const selector = 'main img, .doc-main img, .doc-content img, figure > svg';
  const excluded = 'header, nav, button, [data-doc-image-preview="off"], .doc-image-viewer';
  function eligible(image) {
    if (!image.matches(selector) || image.closest(excluded)) return false;
    const link = image.closest('a');
    if (!link) return true;
    try {
      const href = new URL(link.href, document.baseURI);
      const src = image.currentSrc || image.getAttribute('src');
      return (src && href.href === new URL(src, document.baseURI).href) || /\.(png|jpe?g|gif|webp|avif|svg|bmp)(?:$)/i.test(href.pathname);
    } catch { return false; }
  }
  function prepare(root) {
    const images = root.querySelectorAll ? [...root.querySelectorAll(selector)] : [];
    if (root.matches?.(selector)) images.unshift(root);
    images.filter(eligible).forEach(image => {
      image.setAttribute('data-doc-preview', '');
      if (!image.closest('a')) {
        if (!image.hasAttribute('tabindex')) image.tabIndex = 0;
        if (!image.hasAttribute('role') || image.getAttribute('role') === 'img') image.setAttribute('role', 'button');
        if (!image.hasAttribute('aria-label')) image.setAttribute('aria-label', 'Open image: ' + (image.getAttribute('alt') || image.querySelector('title')?.textContent || 'Figure'));
      }
    });
  }
  const viewer = document.createElement('dialog');
  viewer.className = 'doc-image-viewer';
  viewer.setAttribute('aria-label', 'Image preview');
  const toolbar = document.createElement('div'); toolbar.className = 'doc-image-toolbar';
  const original = document.createElement('a'); original.textContent = 'Open original'; original.target = '_blank'; original.rel = 'noopener';
  const close = document.createElement('button'); close.type = 'button'; close.textContent = 'Close (Esc)'; close.autofocus = true;
  close.setAttribute('aria-label', 'Close image preview');
  toolbar.append(original, close);
  const figure = document.createElement('figure');
  const picture = document.createElement('img'); picture.className = 'doc-image-full';
  const caption = document.createElement('figcaption');
  figure.append(picture, caption); viewer.append(toolbar, figure); document.body.append(viewer);
  let returnFocus = null, objectURL = null;
  function open(image) {
    returnFocus = image.closest('a') || image;
    const description = image.closest('figure')?.querySelector('figcaption')?.textContent?.trim() || image.getAttribute('alt') || image.querySelector('title')?.textContent || 'Figure';
    let source = image.closest('a')?.href || image.currentSrc || image.getAttribute('src');
    if (image.tagName.toLowerCase() === 'svg') {
      const clone = image.cloneNode(true); clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
      objectURL = URL.createObjectURL(new Blob([new XMLSerializer().serializeToString(clone)], {type:'image/svg+xml'}));
      source = objectURL;
    }
    if (!source) return;
    picture.alt = image.getAttribute('alt') || description;
    picture.src = source; original.href = source; caption.textContent = description;
    document.documentElement.classList.add('doc-image-viewer-open'); viewer.showModal();
  }
  viewer.addEventListener('keydown', event => {
    if (event.key !== 'Tab') return;
    if (event.shiftKey && document.activeElement === original) {event.preventDefault(); close.focus();}
    else if (!event.shiftKey && document.activeElement === close) {event.preventDefault(); original.focus();}
  });
  close.addEventListener('click', () => viewer.close());
  viewer.addEventListener('cancel', event => {event.preventDefault(); viewer.close();});
  viewer.addEventListener('click', event => {
    const rect = viewer.getBoundingClientRect();
    if (event.target === viewer && (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom)) viewer.close();
  });
  viewer.addEventListener('close', () => {
    document.documentElement.classList.remove('doc-image-viewer-open');
    picture.removeAttribute('src'); original.removeAttribute('href');
    if (objectURL) {URL.revokeObjectURL(objectURL); objectURL = null;}
    if (returnFocus?.isConnected) returnFocus.focus({preventScroll:true});
    returnFocus = null;
  });
  document.addEventListener('click', event => {
    if (event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    const image = event.target.closest?.('img, figure > svg') || event.target.closest?.('a')?.querySelector('img');
    if (image && eligible(image)) {event.preventDefault(); open(image);}
  });
  document.addEventListener('keydown', event => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    const image = event.target;
    if (image.matches?.('[data-doc-preview]') && !image.closest('a') && eligible(image)) {event.preventDefault(); open(image);}
  });
  prepare(document);
  new MutationObserver(changes => changes.forEach(change => change.addedNodes.forEach(node => {if (node.nodeType === 1 && !viewer.contains(node)) prepare(node);}))).observe(document.body, {childList:true, subtree:true});
})();
// doc-image-viewer:end
