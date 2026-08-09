# NFS Most Wanted (2005) — port, work in progress

Un port neoficial care citește fișierele originale ale jocului (nu le
înlocuiește, nu redistribuie assets) și le randează cu un motor nou,
scris de la zero. Rezultatul muncii de reverse-engineering documentate
în "MWEncyclopedia" + verificări proprii, byte cu byte, pe fișiere reale.

## Ce funcționează acum

- **Rețeaua de drum** (CARP: noduri + segmente) — verificat matematic,
  0 erori pe fișierul de test.
- **Geometrie reală** (clădiri, poduri, drum) — algoritm portat fidel
  dintr-un editor de hartă open-source pentru joc, testat pe fișiere reale.
- **Poziții scenery** (obiecte de decor) — verificat împotriva rețelei
  de drum.
- **Meniu principal** cu cameră care reacționează la schimbarea opțiunii
  (stil NFS).
- **Fizică placeholder** (arcade, nu cea din joc) + mod de zbor liber.

## Ce lipsește

- **AI** — lăsat intenționat pentru mai târziu.
- **Fizica reală** — arhitectura clasei e mapată, formulele nu.
- **Textura pe geometrie** — pipeline-ul de texturi (TPK/DXT) e verificat
  separat (vezi conversația de dezvoltare), dar nu e încă legat de
  geometria din acest build.
- **Sunet, mașini reale, UI-ul jocului** — au nevoie de fișiere separate
  (`SOUND/*.abk`, `CARS/*`, un bundle `FRONTEND`) pe care nu le-am
  procesat încă în acest proiect.

## Fișiere de date necesare

Aplicația NU include fișierele jocului (drepturi de autor + mărime —
`STREAML2RA.BUN` are ~532MB, mult prea mare pentru un APK).

- **Desktop**: pune `L2RA.BUN` și `STREAML2RA.BUN` într-un folder `data/`
  lângă executabil.
- **Android**: instalează APK-ul, apoi copiază aceleași două fișiere cu
  orice aplicație de file manager în:
  `Android/data/com.nfsmwport.app/files/`
  (acest folder e creat de aplicație la prima pornire — dacă nu există,
  pornește aplicația o dată, las-o pe ecranul de meniu, apoi copiază
  fișierele).

## Build

### Desktop (Linux/Windows/macOS via CMake)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Rulează prin GitHub Actions automat la fiecare push (`.github/workflows/build-desktop.yml`).

### Android (APK)

Rulează prin GitHub Actions (`.github/workflows/build-android.yml`) —
folosește Gradle + NDK, ia APK-ul din artifacts după ce rulează workflow-ul.

**Onest**: build-ul Android e prima încercare — n-a putut fi testat local
(fără acces la internet în mediul unde a fost scris, deci raylib +
NDK nu au putut fi descărcate și verificate). Așteaptă-te posibil la
o rundă de debugging pe erorile din log-ul de GitHub Actions.
Build-ul desktop e pe teren mult mai sigur (CMake + raylib e o
combinație foarte bătută).

## Structură

```
src/formats/    — parsoare (chunk-uri EAGL, CARP, geometrie, scenery)
src/render.h    — construiește mesh-uri raylib din datele parsate
src/main.cpp    — meniu + încărcare + lume drivabilă/zburabilă
tests/          — test standalone, fără raylib, validează parsoarele
android/        — proiect Gradle pentru build-ul de APK
```

## Testare locală a parsoarelor (fără raylib)

```
g++ -std=c++17 -O2 -o test tests/test_main.cpp
./test cale/catre/L2RA.BUN cale/catre/STREAML2RA.BUN
```

Verificat pe date reale înainte de a fi inclus în acest proiect:
4385 noduri, 6538 segmente (închidere perfectă), 116/116 obiecte de
geometrie pe secțiunea de test, 199 obiecte scenery.
