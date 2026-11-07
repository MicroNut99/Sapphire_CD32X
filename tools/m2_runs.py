"""m2_runs.py - the recordings M2 runs on (docs/04 §4): the attract demo from power-on (stages 1-4 and everything
between) and stage 5 from a demo state with $0E15 = 4. Each maps to itself: m0_replay.open_host reads the start
and the pokes from the recording."""
STATES = {'demo': 'demo', 'st5': 'st5'}
