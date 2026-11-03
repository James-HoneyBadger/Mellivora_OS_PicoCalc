FROM scratch
ARG VERSION
ARG REPOSITORY
LABEL org.opencontainers.image.title="Mellivora PicoCalc"
LABEL org.opencontainers.image.description="Mellivora PicoCalc RP2040 firmware output"
LABEL org.opencontainers.image.source="https://github.com/${REPOSITORY}"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.version="${VERSION}"
COPY picocalc/build/mellivora_picocalc.uf2 /mellivora_picocalc.uf2
