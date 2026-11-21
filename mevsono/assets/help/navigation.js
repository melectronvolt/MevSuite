// Sur un petit écran, rendre la place au texte dès qu’une rubrique a été choisie.
const sommaire = document.getElementById('contents');
const ecranEtroit = window.matchMedia('(max-width: 820px)');
function adapteSommaire() {
  sommaire.open = !ecranEtroit.matches;
}
adapteSommaire();
ecranEtroit.addEventListener('change', adapteSommaire);

const liens = [...sommaire.querySelectorAll('a[href^="#"]')];
for (const lien of liens) {
  lien.addEventListener('click', () => {
    if (ecranEtroit.matches) sommaire.open = false;
  });
}

// Deux rubriques courtes peuvent être visibles ensemble. Le repère de lecture suit
// le haut de la page. Tout en bas, il désigne la dernière rubrique.
const rubriques = liens.map(lien => document.getElementById(lien.hash.slice(1)));
let lienActif;
function situeLecture() {
  const enBas = Math.ceil(window.scrollY + window.innerHeight) >= document.documentElement.scrollHeight;
  let index = rubriques.length - 1;
  while (!enBas && index >= 0 && rubriques[index].getBoundingClientRect().top > 24) {
    index--;
  }
  const lien = liens[index];
  if (lien === lienActif) return;
  lienActif?.removeAttribute('aria-current');
  lien?.setAttribute('aria-current', 'location');
  lienActif = lien;
}

let miseAJourPrevue = false;
function demandeMiseAJour() {
  if (miseAJourPrevue) return;
  miseAJourPrevue = true;
  requestAnimationFrame(() => {
    miseAJourPrevue = false;
    situeLecture();
  });
}
window.addEventListener('scroll', demandeMiseAJour, { passive: true });
window.addEventListener('resize', demandeMiseAJour);
window.addEventListener('load', demandeMiseAJour);
demandeMiseAJour();
