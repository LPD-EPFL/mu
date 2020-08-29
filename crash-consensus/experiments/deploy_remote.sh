#!/bin/bash

NUM_INSTANCES=3
DORY_REG_IP="lpdquatro4:9999"
STARTING_PORT=6379
PATH_TO_SCRIPTS=~/dory/crash-consensus/experiments

case "$1" in
    stop) STOP=_$1 ;;
    first)
        SSH_TO=`./generate_exports.py $NUM_INSTANCES $DORY_REG_IP $STARTING_PORT 0 GET_HOSTNAME`
        ssh $SSH_TO "cd $PATH_TO_SCRIPTS && ./deploy_sigusr1.sh $NUM_INSTANCES $DORY_REG_IP $STARTING_PORT 0"


     exit 0;;
    *) ;;
esac

SSH_TO_REGISTRY=`echo $DORY_REG_IP | awk -F':' '{print $1}'`
ssh $SSH_TO_REGISTRY "cd $PATH_TO_SCRIPTS && ./reset_registry.sh"

for (( INSTANCE_NUM=0; INSTANCE_NUM<$NUM_INSTANCES; INSTANCE_NUM++ ))
do
    SSH_TO=`./generate_exports.py $NUM_INSTANCES $DORY_REG_IP $STARTING_PORT $INSTANCE_NUM GET_HOSTNAME`

    ssh $SSH_TO "cd $PATH_TO_SCRIPTS && ./deploy_local$STOP.sh $NUM_INSTANCES $DORY_REG_IP $STARTING_PORT $INSTANCE_NUM"
done
