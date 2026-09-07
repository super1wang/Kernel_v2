from pathlib import Path
source=Path('evidence/bootstrap/D1.04/run-encoder-alias-probe.py').read_text()
source=source.replace("'encoder-alias-'","'projection-budget-'").replace('encoder-alias-probe.cpp','projection-budget-probe.cpp')
exec(compile(source,__file__,'exec'))
