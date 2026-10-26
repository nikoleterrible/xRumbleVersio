# RumbleVersio — Techno Rumble Engine pour Desmodus Versio

Firmware alternatif dédié au **Techno Kick Rumble** pour la plateforme **Noise Engineering Versio** (propulsée par l'Electro-Smith Daisy Seed, STM32H750 @ 480 MHz).

---

## ⚡ Architecture Révisée v2.3

La **Version 2.3** apporte des corrections matérielles décisives et une ergonomie live ultra-précise :

- **Correction Matérielle ADC & Mapping 1:1** :
  - Démarrage effectif du convertisseur analogique-numérique (`hw.StartAdc()`).
  - Réalignement physique exact des 7 potentiomètres selon la nomenclature matérielle Versio (Blend sur `KNOB_0`, Regen sur `KNOB_1`, Tone au centre sur `KNOB_2`, Size sur `KNOB_3`, Index sur `KNOB_4`, Speed sur `KNOB_5`, Dense sur `KNOB_6`).
- **Silence Absolu Garanti (< 8% Deadband)** :
  - **`Tone` (Centre Haut)** : Volume du **Downbeat** (temps 1 / sub du kick).
  - **`Blend` (Haut Gauche)** : Volume du **Tap 1** (16e note #2, spatialisé à gauche).
  - **`Regen` (Haut Droite)** : Volume du **Tap 2 / Offbeat** (contre-temps 16e #3/#4, spatialisé à droite).
  - **Silence Absolu Garanti** : Si les 3 potards du haut sont au minimum (< 8%), **la sortie est coupée à 100% (0.000000)**, sans aucune fuite ni résidu de réverbe ou de drive.
- **Horloge Calée sur le Jack FSU (Clock Gate Externe)** :
  - Fini la détection audio automatique approximative sur le kick audio. L'horloge est désormais **100% asservie aux impulsions de gate Eurorack envoyées sur le Jack FSU**.
  - Synchronisation de phase instantanée et mesure de tempo d'une stabilité absolue.
- **Switch 1 : Ajustement Décalage (Micro-Timing / Pocket)** :
  - **BND (Haut)** : *Ahead / Rush* (-12 ms, groove tiré en avant, dynamique agressive).
  - **LRP (Centre)** : *On-Beat* (0 ms, calage neutre exact).
  - **JMP (Bas)** : *Laid-Back Drag* (+18 ms, retard chaloupé / swing techno berlinois).
- **Filtre Résonant SVF 24 dB/oct Ultra-Audible (Speed & Index)** :
  - `Speed` (Knob 5) balaie désormais de **30 Hz à 3200 Hz** (du sub feutré jusqu'à l'ouverture totale des harmoniques médiums).
  - `Index` (Knob 4) règle la résonance Q de 0.707 jusqu'à **6.5 (auto-oscillation acide mordante)**.
- **Bouton FSU (Poussoir) : Effet Freeze Wash Décorrélé** :
  - L'appui manuel sur le bouton rond FSU déclenche instantanément un wash infini (feedback 99.5%) pour créer des montées de tension en live, indépendant de l'horloge sur le jack.

---

## 🎛️ Mapping de la Façade (1:1 Desmodus Versio)

```
        [  BLEND  ]                 [  REGEN  ]
       (Tap 1 / 16e #2)           (Tap 2 / Offbeat)
               \                   /
                 \   [  TONE  ]   /
                   (DOWNBEAT SUB)
                         |
        [  SPEED  ]      |          [  SIZE   ]
      (Cutoff 30-3200)   |       (Reverb Space / Dry)
               \         |         /
                 \   [ INDEX ]   /
                   (Resonance Q)

   [ SW 0 ]                             [ DENSE ]
 (LIM/DST/SHM)                         (Drive Saturation)
   [ SW 1 ]           ( FSU )
 (AHEAD/0/DRAG)  [FREEZE WASH FX]

    [ BLEND ]  [ TONE ]   [ REGEN ]  [ DENSE ]
    [ SPEED ]  [ INDEX ]  [ SIZE ]   [  FSU  ]
   ================ JAGGED BAND ================
    [ IN L  ]  [ IN R ]   [ OUT L ]  [ OUT R ]
```

| Contrôle Desmodus | Emplacement Physique | Pin Hardware | Rôle DSP Rumble v2.3 | Description Musicale |
| :--- | :--- | :--- | :--- | :--- |
| **Blend** | **Haut Gauche** | `KNOB_0` | **TAP 1 Level** | Volume 2e double-croche (pan Gauche). < 8% = strict 0.0000. |
| **Tone** | **Centre Haut** (en V inversé) | `KNOB_2` | **DOWNBEAT Level** | Volume Sub sur le Temps 1 (pan Centre). < 8% = strict 0.0000. |
| **Regen** | **Haut Droite** | `KNOB_1` | **TAP 2 / OFFBEAT** | Volume Contre-temps (pan Droite). < 8% = strict 0.0000. |
| **Speed** | **2e rangée Gauche** | `KNOB_5` | **SVF CUTOFF** | Cutoff SVF 24 dB/oct : balayage franc de 30 Hz à 3200 Hz. |
| **Index** | **Centre Médian** (en V inversé) | `KNOB_4` | **SVF RESONANCE (Q)** | Résonance Q de 0.707 à 6.5 (acide, auto-oscillation). |
| **Size** | **2e rangée Droite** | `KNOB_3` | **SUB REVERB SPACE** | Espace de réverbe sub (0 = sec percutant, 1 = warehouse). |
| **Dense** | **En bas à Droite** | `KNOB_6` | **DRIVE / SAT** | Saturation harmonique unitaire (1.0x à 4.2x). |
| **Switch 0 (Haut)** | **Gauche (LIM / DST / SHM)** | `SW_0` | **SAT MODEL** | LIM (Sub Pur), DST (Warm Tape), SHM (Fat Crunch). |
| **Switch 1 (Bas)** | **Gauche (BND / LRP / JMP)** | `SW_1` | **MICRO-TIMING** | BND (-12ms Ahead), LRP (0ms On-Beat), JMP (+18ms Drag). |
| **Bouton FSU** | **Centre (Bouton rond)** | `TAP` | **FREEZE WASH FX** | Pression = Wash infini (reverb/delay 99.5%) pour les breaks. |
| **Jack FSU** | **2e rangée Droite Jacks** | `GATE IN` | **CLOCK GATE IN** | Entrée Clock Eurorack (déclenche & synchronise le tempo). |
| **4 LEDs** | **Entre Blend & Regen** | `LED 0..3` | **K / T1 / T2 / T3** | Visualisation du rythme. Pulsation violette pendant le Freeze. |

---

## 🔌 Câblage & Routage Audio

- **IN L** : Audio entrant (Kick drum).
- **OUT L** : Sortie Stéréo Gauche (100% Wet Pure Rumble).
- **OUT R** : Sortie Stéréo Droite (100% Wet Pure Rumble).
- **JACK FSU (GATE IN)** : **Horloge Eurorack (Clock / Trigger / Gate)**.

---

## 🔨 Compilation & Flashage

```powershell
.\build_firmware.bat
```

Le fichier binaire généré `RumbleVersio.bin` (91.4 KB) est prêt à être flashé via le **Web Programmer officiel Electro-Smith**.
