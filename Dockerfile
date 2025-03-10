FROM lsstts/develop-env:c0025.005

USER root
RUN chmod a+rwX -R /home/saluser/
USER saluser

WORKDIR /home/saluser

RUN source .setup.sh \
    && mamba install -y readline yaml-cpp boost-cpp catch2 \
    && echo > .crio_setup.sh -e \
echo "Configuring cRIO development environment" \\n\
source /home/saluser/.setup_salobj.sh \\n\
export PATH=\$CONDA_PREFIX/bin:\$PATH \\n\
export LIBS="-L\$CONDA_PREFIX/lib" \\n\
export CPP_FLAGS="-I\$CONDA_PREFIX/include" \\n

SHELL ["/bin/bash", "-lc"]
