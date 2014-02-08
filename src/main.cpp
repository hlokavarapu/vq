// Copyright (c) 2012-2014 Eric M. Heien, Michael K. Sachs, John B. Rundle
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "VCSimulation.h"

// Plugin #includes
#include "HDF5DataShare.h"

// File parsing and output related
#include "EqSimFileOutput.h"
#include "EqSimFileParse.h"
#include "GreensFileOutput.h"
#include "CheckpointFileOutput.h"
#include "CheckpointFileParse.h"

// Simulation related
#include "BadFaultKill.h"
#include "BASSAftershocks.h"
#include "BlockValCompute.h"
#include "EventRecorder.h"
#include "GracefulQuit.h"
#include "GreensInit.h"
#include "GreensKillInteraction.h"
#include "ProgressMonitor.h"
#include "RunEvent.h"
#include "SanityCheck.h"
#include "UpdateBlockStress.h"
#include "VCInitBlocks.h"

int main (int argc, char **argv) {
    PluginID        read_eqsim_file, init_blocks, block_val_compute;
    PluginID        greens_init, greens_outfile, bad_fault_kill;
    PluginID        greens_kill, update_block_stress, run_event;
    PluginID        sanity_checking, bass_model_aftershocks, display_progress, state_output_file;
    PluginID        eqsim_output_file, h5_data_share, vc_events_output_file, graceful_quit;
    VCSimulation    *vc_sim;

    vc_sim = new VCSimulation(argc, argv);

    // ************************************************************
    // ** Define plugins and whether they are active
    // ************************************************************
    // EqSim files are parsed if a geometry file name is specified
    read_eqsim_file = vc_sim->registerPlugin(new EqSimFileParse, !vc_sim->getEqSimGeometryFile().empty());

    // Calculate block values if we aren't using EqSim files
    block_val_compute = vc_sim->registerPlugin(new BlockValCompute, vc_sim->getEqSimGeometryFile().empty());

    // Calculate Greens function if a calculation method is specified
    greens_init = vc_sim->registerPlugin(new GreensInit, vc_sim->getGreensCalcMethod() != GREENS_CALC_NONE);

    // Write the Greens values to a file if a file name is specified
    greens_outfile = vc_sim->registerPlugin(new GreensFileOutput, !vc_sim->getGreensOutfile().empty());

    // Kill faults that drop below a certain CFF value
    bad_fault_kill = vc_sim->registerPlugin(new BadFaultKill, vc_sim->getFaultKillCFF() < 0);

    // Implement Greens matrix killing if the kill distance is greater than 0
    greens_kill = vc_sim->registerPlugin(new GreensKillInteraction, vc_sim->getGreensKillDistance() > 0);

    // Implement sanity checking if requested
    sanity_checking = vc_sim->registerPlugin(new SanityCheck, vc_sim->doSanityCheck());

    // Implement BASS aftershocks if the number of BASS generations is more than 0
    bass_model_aftershocks = vc_sim->registerPlugin(new BASSAftershocks, vc_sim->getBASSMaxGenerations() != 0);

    // Display progress if the progress display period is more than 0
    display_progress = vc_sim->registerPlugin(new ProgressMonitor, vc_sim->getProgressPeriod() > 0);

    // Write the simulation state if the period is more than zero
    state_output_file = vc_sim->registerPlugin(new CheckpointFileOutput, vc_sim->getCheckpointPeriod() > 0);

    // Write an EqSim event file if the output file is specified
    eqsim_output_file = vc_sim->registerPlugin(new EqSimFileOutput, !vc_sim->getEqSimOutputFile().empty());

    // Write events in the old output format if the file name is specified
    vc_events_output_file = vc_sim->registerPlugin(new EventRecorder, !vc_sim->getEventsFile().empty());

    // These plugins are always active in a simulation
    init_blocks = vc_sim->registerPlugin(new VCInitBlocks, true);
    update_block_stress = vc_sim->registerPlugin(new UpdateBlockStress, true);
    run_event = vc_sim->registerPlugin(new RunEvent, true);
    h5_data_share = vc_sim->registerPlugin(new HDF5DataShare, true);
    graceful_quit = vc_sim->registerPlugin(new GracefulQuit, true);

    // ************************************************************
    // ** Specify plugin dependencies
    // ************************************************************
    // Block initialization must occur after the blocks are read in
    vc_sim->registerDependence(init_blocks, read_eqsim_file, DEP_OPTIONAL);

    // Check for quit file before doing any intensive calculation
    vc_sim->registerDependence(graceful_quit, init_blocks, DEP_OPTIONAL);

    // Greens calculation occurs first, then initial Greens/system value output
    vc_sim->registerDependence(greens_init, graceful_quit, DEP_OPTIONAL);

    // Block modifications occur after block initialization
    vc_sim->registerDependence(block_val_compute, greens_init, DEP_OPTIONAL);
    vc_sim->registerDependence(greens_outfile, greens_init, DEP_REQUIRE);
    vc_sim->registerDependence(greens_kill, greens_init, DEP_REQUIRE);
    vc_sim->registerDependence(greens_outfile, greens_kill, DEP_OPTIONAL);

    // The core of the simulation, which must occur after Greens function is calculated
    vc_sim->registerDependence(update_block_stress, block_val_compute, DEP_OPTIONAL);
    vc_sim->registerDependence(update_block_stress, greens_kill, DEP_OPTIONAL);
    vc_sim->registerDependence(run_event, update_block_stress, DEP_REQUIRE);

    // BASS aftershocks are added after the initial event is processed
    vc_sim->registerDependence(bass_model_aftershocks, run_event, DEP_REQUIRE);

    // Sanity checking occurs after the events are run
    vc_sim->registerDependence(sanity_checking, run_event, DEP_OPTIONAL);
    vc_sim->registerDependence(bad_fault_kill, run_event, DEP_OPTIONAL);

    // Output occurs after events (including aftershocks) are finished
    vc_sim->registerDependence(display_progress, run_event, DEP_OPTIONAL);
    vc_sim->registerDependence(state_output_file, run_event, DEP_OPTIONAL);
    vc_sim->registerDependence(eqsim_output_file, bass_model_aftershocks, DEP_OPTIONAL);
    vc_sim->registerDependence(h5_data_share, bass_model_aftershocks, DEP_OPTIONAL);
    vc_sim->registerDependence(vc_events_output_file, bass_model_aftershocks, DEP_OPTIONAL);

    //vc_sim->writeDOT(vc_sim->errConsole());

    // ************************************************************
    // ** Initialize and run the simulation
    // ************************************************************
    vc_sim->init();
    vc_sim->run();
    vc_sim->finish();

    vc_sim->printTimers();

    delete vc_sim;

    return 0;
}
