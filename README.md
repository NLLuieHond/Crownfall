# Crownfall voor de Nintendo 3DS

Je hebt geen programmeerkennis nodig. Je laat een gratis server op internet het spel voor je bouwen.

## Stap 1 - Maak een gratis GitHub-account
1. Ga naar https://github.com en kies "Sign up". Maak een account.

## Stap 2 - Zet het project online
1. Klik rechtsboven op "+" en kies "New repository". Naam: `crownfall`. Kies "Public". Klik "Create repository".
2. Pak de zip uit op je computer.
3. Klik op "uploading an existing file". Sleep ALLE bestanden en mappen uit de uitgepakte map erin (ook de verborgen map `.github`: zet in de Verkenner "Verborgen items" aan onder Beeld).
   Let op: de map `.github` moet er echt bij zitten, anders start het bouwen niet.
4. Klik onderaan op "Commit changes".

## Stap 3 - Laat het bouwen
1. Klik bovenin op het tabblad "Actions". Het bouwen start vanzelf (duurt ongeveer 3 tot 6 minuten).
2. Wacht tot er een groen vinkje staat. Klik op de naam van de run.
3. Onderaan bij "Artifacts" klik je op `Crownfall-3DS`. Je krijgt een zip.
4. Is het rood? Maak een screenshot van de foutmelding en stuur die naar mij. Dan los ik het op.

## Stap 4 - Op je 3DS zetten
**Optie A (makkelijkst): .3dsx**
1. Zet de SD-kaart van je 3DS in je computer.
2. Kopieer `crownfall.3dsx` naar de map `3ds` op de SD-kaart.
3. Start op je 3DS de Homebrew Launcher en kies Crownfall.

**Optie B: installeren met icoontje in het hoofdmenu (.cia)**
1. Kopieer `crownfall.cia` naar de SD-kaart.
2. Open op de 3DS het programma FBI, ga naar SD > het bestand > "Install and delete CIA".
3. Het spel staat nu met icoon in je hoofdmenu.
(Is er geen .cia in de download, dan is alleen dat onderdeel mislukt. Gebruik dan optie A.)

## Bediening
- Tik op het onderste scherm (aanraken). Het pijltjeskruis werkt voor lopen.
- START = einde beurt, SELECT = afsluiten.
- Je voortgang staat in `3ds/crownfall` op de SD-kaart. Fouten staan in `log.txt` daar.

## Let op
Dit is de eerste versie. Ik heb het spel getest met een computer-nabootsing van de 3DS, maar niet op een echte 3DS. Werkt er iets niet? Stuur me een foto of de inhoud van `log.txt`.
