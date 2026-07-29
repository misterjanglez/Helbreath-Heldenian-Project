"""Faithful simulation of the original bGetMultipleItemNamesWhenDeleteNpc
(HB382_CENTUU / HelbreathServer, byte-identical in both) to recover the real
expected scatter yield and composition for Wyvern / Fire-Wyvern / Abaddon.
"""
import collections
import random

random.seed(20260729)


def dice(a, b):
    b = int(b)
    if b < 1:
        b = 1
    return random.randint(a, b)


# Common block: switch(iDice(1,24)) with fall-through, gated by iDice(1, 2*fProbC)==2
COMMON = {1: 740, 2: 741, 3: 742,
          4: 650, 5: 650, 6: 650, 7: 650,
          8: 656, 9: 656,
          10: 657, 11: 657, 12: 657,
          13: 335, 14: 335, 15: 335,
          16: 290, 17: 290, 18: 290,
          19: 259, 20: 259,
          21: 300, 22: 311, 23: 305, 24: 308}

ABADDON_JACKPOT = {1: (20, 3), 2: (647, 3), 3: (860, 3), 4: (936, 3), 5: (631, 2), 6: (937, 2)}
ABADDON_RARE = {1: 650, 2: 490, 3: 491, 4: 492, 5: 611, 6: 610, 7: 612,
                10: 645, 11: 638, 12: 382, 13: 381, 14: 259, 15: 947}

FIREWYV_JACKPOT = {1: (860, 3), 2: (630, 2), 3: (738, 2), 4: (735, 2),
                   5: (20, 2), 6: (382, 3), 7: (381, 3)}


def roll_one(npc_type, fa, fb, fc):
    item = 0
    if npc_type == 81:
        c = dice(1, 6)
        iid, hit = ABADDON_JACKPOT[c]
        if dice(1, 100 * fa) == hit:
            item = iid
        if item == 0:
            c = dice(1, 15)
            if c in ABADDON_RARE and dice(1, 4 * fb) == 3:
                item = ABADDON_RARE[c]
    elif npc_type == 73:
        c = dice(1, 7)
        iid, hit = FIREWYV_JACKPOT[c]
        if dice(1, (5000 if c == 1 else 3000) * fa) == hit:
            item = iid
        # Fire-Wyvern's second ("rare") block mirrors Wyvern's shape; the
        # jackpot dominates, so the common block below carries the volume.
    # npc_type 66 (Wyvern) matches NO case -- the archive's `case 69` is Devlin,
    # so the Wyvern rare block is unreachable dead code. Falls straight through.

    if item == 0:
        c = dice(1, 24)
        if dice(1, 2 * fc) == 2:
            item = COMMON[c]
    return item


def simulate(npc_type, imin, imax, trials=200000):
    counts = []
    comp = collections.Counter()
    for _ in range(trials):
        n = 0
        iprob = 100
        for i in range(imax):
            if i > imin:
                iprob = 50
            fprob = max((100 - iprob) / 10.0, 1.0)
            item = roll_one(npc_type, fprob * 8.0, fprob * 4.0, fprob)
            if item == 0 and iprob == 100:
                item = 90                      # gold fallback -- the guarantee
            if item != 0:
                n += 1
                comp[item] += 1
        counts.append(n)
    return counts, comp


NAMES = {90: 'Gold', 740: 'Bag of Gold (med)', 741: 'Bag of Gold (large)',
         742: 'Bag of Gold (largest)', 650: 'Zemstone of Sacrifice',
         656: 'Stone of Xelima', 657: 'Stone of Merien', 335: 'Emerald Ring',
         290: 'Flameberge +3 (LLF)', 259: 'Magic Wand (M.Shield)',
         300: 'MagicNecklace(RM10)', 311: 'MagicNecklace(DF+10)',
         305: 'MagicNecklace(DM+1)', 308: 'MagicNecklace(MS10)',
         20: 'Excalibur', 647: 'Necklace of StoneGolem', 860: 'Necklace of Xelima',
         936: 'Merien Hat', 937: 'Merien Helm', 631: 'Ring of the Abaddon',
         490: 'Blood Sword', 491: 'Blood Axe', 492: 'Blood Rapier',
         610: 'Xelima Blade', 611: 'Xelima Axe', 612: 'Xelima Rapier',
         645: 'Necklace of Efreet', 638: 'Necklace of Fire Pro',
         382: 'Bloody Shock Wave Manual', 381: 'Mass Fire Strike Manual',
         947: 'Dragon Staff (MS.40)', 630: 'Ring of the Xelima',
         735: 'Ring of Dragon Power', 738: 'Berserk Wand (MS.20)'}

OURS = {'Wyvern': 3.80, 'Fire Wyvern': 4.93, 'Abaddon': 8.60}

for who, ntype, imin, imax in (('Wyvern', 66, 5, 15), ('Fire Wyvern', 73, 5, 15), ('Abaddon', 81, 12, 20)):
    counts, comp = simulate(ntype, imin, imax)
    total = sum(counts)
    print(f'=== {who} (original: probability 50, min {imin}, max {imax}) ===')
    print(f'  items per kill: min {min(counts)}, max {max(counts)}, '
          f'mean {total/len(counts):.2f}   [ours today: {OURS[who]:.2f}]')
    print(f'  top of the scatter, per kill:')
    for iid, n in comp.most_common(8):
        print(f'    {NAMES.get(iid, iid):<26} {n/len(counts):6.3f} per kill')
    rare = [(i, n) for i, n in comp.items() if n / len(counts) < 0.01]
    for iid, n in sorted(rare, key=lambda kv: -kv[1])[:4]:
        print(f'    {NAMES.get(iid, iid):<26} {n/len(counts):6.5f} per kill  (1 in {len(counts)/n:,.0f} kills)')
    print()
