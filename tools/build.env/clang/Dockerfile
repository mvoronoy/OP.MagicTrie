# FROM fedora:31
FROM fedora:latest

WORKDIR /usr/src/
#    dnf -y install llvm-toolset-8.0.1 &&\
#    scl enable llvm-toolset-8.0.1 bash && \

RUN dnf -y update && dnf -y upgrade &&\
    dnf -y install scl-utils openssh-server &&\
    dnf -y install which java python3.8 net-tools tcpdump zlib bzip2 cmake3 git wget &&\
    dnf -y install clang llvm llvm-devel clang-devel gdb &&\
    dnf -y  install ninja-build

#--enablerepo=PowerTools

ENV PYTHONHOME /usr/

#start the container from bash
#CMD [ "/bin/bash" ]
