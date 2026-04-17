FROM scratch
LABEL org.opencontainers.image.title="Mellivora PicoCalc"
LABEL org.opencontainers.image.description="Mellivora PicoCalc RP2040 firmware output"
LABEL org.opencontainers.image.source="https://github.com/James-HoneyBadger/Mellivora_OS"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.version="2.0.0"
COPY picocalc/build/mellivora_picocalc.uf2 /mellivora_picocalc.uf2
