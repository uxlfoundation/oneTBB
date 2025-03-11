DIAGRAM_SOURCES = $(wildcard *.puml)
DIAGRAM_PDFS = $(patsubst %.puml,%.pdf,$(DIAGRAM_SOURCES))
DIAGRAM_PNGS = $(patsubst %.puml,%.png,$(DIAGRAM_SOURCES))
DIAGRAM_EPSS = $(patsubst %.puml,%.eps,$(DIAGRAM_SOURCES))
PUML=plantuml

.SUFFIXES : .puml .eps .pdf .png .tmp
.PHONY: clean

all: diagrams
diagrams: $(DIAGRAM_PDFS) $(DIAGRAM_PNGS)
	



clean:
	rm -f $(DIAGRAM_EPSS)
	rm -f $(DIAGRAM_PDFS)
	rm -f $(DIAGRAM_PNGS)

.puml.png:
	$(PUML) -config puml.cfg -tpng $<

.puml.eps:
	$(PUML) -config puml.cfg -teps $<

.eps.pdf:
	epstopdf $<

