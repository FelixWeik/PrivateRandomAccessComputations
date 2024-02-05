# PRAC PoPETs 2024 artifact

Sajin Sasy, ssasy@uwaterloo.ca  
Adithya Vadapalli, avadapalli@cse.iitk.ac.in  
Ian Goldberg, iang@uwaterloo.ca

This repo contains scripts to run the code and reproduce the graphs in our paper:

Sajin Sasy, Adithya Vadapalli, Ian Goldberg. PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures. Proceedings on Privacy Enhancing Technologies 2024(3). [https://eprint.iacr.org/2023/1897](https://eprint.iacr.org/2023/1897)

It also loads four other repos to compare our work to previous work:

  - Duoram
  - Floram
  - 3-party Circuit ORAM
  - Ramen

## Requirements

To use this repo, you will need:

  - A system with an x86 processor that supports AVX2 instructions.  This
    instruction set was released in 2013, so most recent processors should
    be fine.  On Linux, `grep avx2 /proc/cpuinfo` to see if your CPU can be used (if the output shows you CPU flags, then it can be; if the output is empty, it cannot).

  - A basic Linux installation, with `git` and `docker` installed.  We
    have tested it on Ubuntu 20.04 and Ubuntu 22.04, with
    `apt install git docker.io`

  - At least 16 GB of available RAM.  If you only have 16 GB, you will not be able to run the largest experiments (sizes 2^28 and 2^30, and for Ramen, 2^26), however.  Larger amounts of memory will also make some of the preprocessing steps faster, because more of the memory-intensive preprocessing can happen in parallel rather than in series.

## Quick start

  - Clone this repo and check out the `popets-repro` branch

        git clone https://git-crysp.uwaterloo.ca/iang/prac
        cd prac
        git checkout popets-repro

  - Build the dockers; this will also download and build the dockers for the four comparator systems (approximate time: 15 minutes)

        cd repro
        ./build-all-dockers

  - Run the "kick the tires" test (approximate time: 2 minutes)

        ./repro-all-dockers test

  - The output will have a stanza at the bottom labelled `===== Collated output =====`. In it, you will see the test output; for example:

        PRACPreprc read 16 1 2.835 s
        PRACPreprc read 16 1 2.48502604166667 KiB
        PRACPreprc read 16 1 35 latencies
        PRACOnln read 16 1 0.208 s
        PRACOnln read 16 1 0.0078125 KiB
        PRACOnln read 16 1 2 latencies
        PRACTotl read 16 1 3.043 s
        PRACTotl read 16 1 2.49283854166667 KiB


        3PDuoramOnln readwrite 16 1us 100gbit 2 0.0106452 s
        3PDuoramOnln readwrite 16 1us 100gbit 2 0.104166666666667 KiB
        3PDuoramTotl readwrite 16 1us 100gbit 2 0.7768892 s
        3PDuoramTotl readwrite 16 1us 100gbit 2 12.7916666666667 KiB


        Floram read 16 1 7.139727 s
        Floram read 16 1 2964.69140625 KiB


        CircuitORAMOnln read 16 1 6.274 s
        CircuitORAMOnln read 16 1 355.3125 KiB
        CircuitORAMTotl read 16 1 7.135 s
        CircuitORAMTotl read 16 1 2478.5 KiB


        Ramen read 16 1 2.457118 s
        Ramen read 16 1 1.52962239583333 KiB

  - What you see here are the outputs of the kick-the-tires tests for the five systems (PRAC and the four comparator systems).  PRAC, Duoram, and Circuit ORAM separately report their online and total (preprocessing plus online) costs; PRAC additionally explicitly reports its preprocessing costs.  The costs reported are times (in seconds), average bandwidth used per party (in KiB = 1024 bytes), and for PRAC, latencies (number of sequential messages sent).

  - The output fields are:
    - system and phase
    - experiment mode
    - log<sub>2</sub> of the number of 64-bit words in the ORAM
    - (for Duoram) network latency and speed
    - the number of operations performed
    - the time, bandwidth, or number of latencies
  
  - You should see the same set of 20 lines as shown above.  The bandwidths and latencies should match, but the exact timings will vary according to your hardware.
  - If you run the test more than once, you will see means and stddevs of all of your runs.

## Performance tuning

There are a few environment variables you can set to tune the performance to the hardware configuration of your machine.

### Simple version

If you don't want to do the more detailed tuning below, just do this one:

  - Set the environment variable `PRAC_MAXGB` to the number of _available_ GiB of RAM you have (minimum 16).  In bash, for example:

        export PRAC_MAXGB=64

### Detailed version

**These are a bit in the weeds and are definitely optional.**

  - **If you have a NUMA machine with at least 3 NUMA nodes**, you'll probably want to run each party on its own NUMA node.  You can use the numactl command for this, as follows:

        export PRAC_NUMA_P0="numactl -N 0 -m 0"
        export PRAC_NUMA_P1="numactl -N 1 -m 1"
        export PRAC_NUMA_P2="numactl -N 2 -m 2"

  - In addition, if you have hyperthreading turned on, you may want to only use one thread per physical core.  In this case, replace (for example) `-N 0` with `-C 0-17`, `-N 1` with `-C 18-33`, etc., if you have 18-core CPUs, and the "A sides" of the cores are sequentially assigned in `/proc/cpuinfo`.
  - Set `PRAC_P0_MAXGB` to the available GiB of RAM on _each_ NUMA node.
  
  - **If you have a NUMA machine with 2 NUMA nodes**, you'll want P0 and P2 on one node, and P1 on the other.  Set `PRAC_P02_MAXGB` to the available GiB of RAM on _each_ NUMA node.  For example, for a machine with 2 NUMA nodes, each with a 40-core CPU and 1000 GiB of available RAM, set:

        export PRAC_NUMA_P0="numactl -m 0 -C 0-19"
        export PRAC_NUMA_P1="numactl -m 1 -C 40-59"
        export PRAC_NUMA_P2="numactl -m 0 -C 20-39"
        export PRAC_P02_MAXGB=1000

  - The above will give each party 20 cores (the last 20 cores, as well as all the "B sides" of hyperthreading, will be unused).

  - **If you have a regular (non-NUMA) machine**, you may still want to assign each party to a separate set of one-third of the "A side" cores.  For example if you have a 16-core (32-hyperthread) CPU, with 0-15 being the A sides, and 16-31 being the B sides, with 64 GiB of available RAM, you might set:

        export PRAC_NUMA_P0="numactl -C 0-4"
        export PRAC_NUMA_P1="numactl -C 5-9"
        export PRAC_NUMA_P2="numactl -C 10-14"
        export PRAC_MAXGB=64

  - **Note**: you do _not_ have to have `numactl` installed on the host machine; it is run _inside_ the dockers, which already have it installed.

## Running the reproduction experiments

### The docker experiments (Figures 6, 7, and 8 and Tables 3 and 4)

To reproduce the data for these figures and tables is then simple. Ensure you're in the `repro` directory, and then run:

  - <code>./repro-all-dockers all _numiters_</code>

where <code>_numiters_</code> is the number of iterations of the experiments to run.  Each iteration takes a significant time (see below) to run all the experiments for all five systems, so you might want to keep this small.  We used `3` for the results in the paper, but the stddevs are extremely small, so if you just want to verify the results, `1` might even be good enough.

If you only want to run some of the experiments, you can replace `all` with `fig6`, `fig7`, `fig8`, `tab3`, or `tab4`.

You can also run `./repro-all-dockers none`, which will not run any new experiments, but will output the results accumulated from previous runs.

#### Approximate expected runtimes for the experiments

The following table gives approximate runtimes to expect for _each iteration_ of the experiments, broken out by experiment and ORAM system used.  The bottom right corner (a little over **50 hours**) is therefore the total time to expect for a run of `./repro-all-dockers all 1`.  If you have a system with closer to 16 GB available RAM than 1 or 2 TB, then, as above, some of the times may be larger because preprocessing takes longer, but some may be smaller because the largest two sizes of experiment will not be able to be run at all.  Note that not every system appears in every figure or table.

|System      |fig6    |fig7    |fig8    |tab3 |tab4  |all     |
|------------|--------|--------|--------|-----|------|--------|
|PRAC        |15 min  |30 min  |90 min  |1 min|10 min|2.5 hr  |
|Duoram      |2.5 hr  |        |        |     |      |2.5 hr  |
|Floram      |4.5 hr  |2.75 hr |        |     |      |7.25 hr |
|Circuit ORAM|9.75 hr |6.25 hr |18.75 hr|     |      |34.75 hr|
|Ramen       |75 min  |45 min  |1.75 hr |     |      |3.75 hr |
|**Total**   |18.25 hr|10.25 hr|22 hr   |1 min|10 min|50.75 hr|


#### The output

Runs of `./repro-all-dockers` other than the `test` run will output the data for Figures 6(a), 6(b), 6(c), 7(a), 7(b), 7(c), 8(a), 8(b), 8(c), and Tables 3 and 4, clearly labelled and separated into the data for each line in each subfigure.  Running multiple iterations, or running `./repro-all-dockers` multiple times, will accumulate data, and means and stddevs will be output for all data points when more than one run has completed. From this data, you should be able to verify our major claims (though depending on your hardware, the exact numbers will surely vary).

### The live network experiments (Figure 9)

These experiments are meant to be run on real hosts over a live network, not in dockers, and so they are a little more complicated to set up.

You will need four machines:

  - Your local "control" machine.  This is the machine that will orchestrate the experiments and parse the results, but not run any ORAM protocols itself. You will not need root access on this machine.
  - Three "MPC" machines somewhere on the Internet to run the three parties in the ORAM protocols (PRAC, 3P Circuit ORAM, and Ramen). You will need root access in order to install dependencies with "apt install". These MPC machines must be able to accept incoming ssh connections from the control machine, and incoming connections on various ports used by the three ORAM protocols from the other MPC machines.

Each machine should be running Ubuntu 22.04 (20.04 may also work).  The three MPC machines must each have at least 3.5 GiB of available RAM. You must have `git` installed on each of the four machines:

        apt update && apt install -y git

Ensure you have ssh set up so that you can ssh from the control machine to each of the MPC machines without a password or other interaction.  It's fine if you need extra arguments to ssh (such as "-i keyfile").

On each of the machines, clone this repo, check out the `popets-repro` branch, and cd into the repro directory:

        git clone https://git-crysp.uwaterloo.ca/iang/prac
        cd prac
        git checkout popets-repro
        cd repro

**On the three MPC machines**: first, install the dependencies as root (about 2 minutes):

        sudo ./setup-fig9-livenet

then, build the code (about 10 minutes):

        ./build-fig9-livenet

The build step does not need to be done as root.

**The remaining instructions are on the control machine.**

Clone (but no need to build) the comparator repos, to obtain the testing scripts:

        ./clone-fig9-livenet

Set the following environment variables to specify how to connect to each of the three MPC machines (for players P0, P1, and P2), and where to find the prac repo you cloned:

  - `PRAC_SSH_P0_USERHOST`: the user@host to ssh to for P0
  - `PRAC_SSH_P0_SSHOPTS`: any options to ssh you need to ssh to P0
  - `PRAC_SSH_P0_IP`: the IP address P0 can listen for connections on
  - `PRAC_SSH_P0_DIR`: the directory relative to the homedir where the prac repo is checked out and built

and similarly for P1 and P2.  For example:

        export PRAC_SSH_P0_USERHOST=ubuntu@54.193.126.4
        export PRAC_SSH_P0_SSHOPTS=
        export PRAC_SSH_P0_IP=54.193.126.4
        export PRAC_SSH_P0_DIR=prac

        export PRAC_SSH_P1_USERHOST=ubuntu@3.142.251.188
        export PRAC_SSH_P1_SSHOPTS=
        export PRAC_SSH_P1_IP=3.142.251.188
        export PRAC_SSH_P1_DIR=prac

        export PRAC_SSH_P2_USERHOST=ubuntu@35.182.75.154
        export PRAC_SSH_P2_SSHOPTS=
        export PRAC_SSH_P2_IP=35.182.75.154
        export PRAC_SSH_P2_DIR=prac

Then run the experiments with:

  - <code>./repro-fig9-livenet _numiters_</code>

The time to run these experiments will depend heavily on the latencies between the three MPC machines.  In our setup, the latencies varied between 10 and 35 ms, and the running time for each iteration was about 12 hours.

You can use <code>./repro-fig9-livenet 0</code> to not run any more iterations, but just to re-output the results accumulated in previous runs.

#### The output

The output will show the data for the lines in Figure 9 of the paper, as well as the three numbers in the last sentence of Section 8 of the paper: the measured timing for the largest experiment for PRAC, and the lower bound estimates (counting _only_ the DORAM operations of _only_ the heap extraction components of the protocol) for Ramen and 3P Circuit ORAM.

----------
Credit: format and some text adapted from our prior [README for our Duoram artifact](https://git-crysp.uwaterloo.ca/avadapal/duoram/src/master/README.md).
