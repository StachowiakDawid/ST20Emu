FROM --platform=linux/386 debian/eol:lenny
RUN apt-get update -o Acquire::Check-Valid-Until=false && \
    apt-get install -y --force-yes gcc make bison flex patch bzip2 perl texinfo && apt-get clean
WORKDIR /work
