// Repère dans le sommaire la rubrique que le lecteur a sous les yeux.
// Les liens d’ancrage fonctionnent aussi lorsque JavaScript est désactivé.
if ("IntersectionObserver" in window) {
  const liens = new Map();
  for (const lien of document.querySelectorAll('nav.toc a[href^="#"]')) {
    liens.set(lien.hash.slice(1), lien);
  }

  let lienCourant;
  const observateur = new IntersectionObserver((entrees) => {
    for (const entree of entrees) {
      if (!entree.isIntersecting) continue;
      lienCourant?.removeAttribute("aria-current");
      lienCourant = liens.get(entree.target.id);
      lienCourant.setAttribute("aria-current", "location");
    }
  }, { rootMargin: "-10% 0px -75% 0px" });

  for (const identifiant of liens.keys()) {
    observateur.observe(document.getElementById(identifiant));
  }
}
