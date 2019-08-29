#!/usr/bin/env python
#
# https://nose.readthedocs.org/en/latest/
# https://nose.readthedocs.org/en/latest/testing.html

import epics
import unittest
import os
import sys
from motor_lib import motor_lib
###

class Test(unittest.TestCase):
    lib = motor_lib()
    motor = os.getenv("TESTEDMOTORAXIS")
    epics.caput(motor + '-DbgStrToLOG', "Start " + os.path.basename(__file__)[0:20])

    hlm = epics.caget(motor + '.HLM')
    llm = epics.caget(motor + '.LLM')

    per30_UserPosition  = round((7 * llm + 3 * hlm) / 10)

    range_postion    = hlm - llm
    saved_JVEL       = epics.caget(motor + '.JVEL')
    moving_velocity  = epics.caget(motor + '.VELO')
    acceleration     = epics.caget(motor + '.ACCL')
    msta             = int(epics.caget(motor + '.MSTA'))
    if (saved_JVEL != 0.0):
        jogging_velocity = saved_JVEL;
    else:
        jogging_velocity = moving_velocity / 2.0;

    print 'llm=%f hlm=%f per30_UserPosition=%f' % (llm, hlm, per30_UserPosition)
    print 'saved_JVEL=%f jogging_velocity=%f' % (saved_JVEL, jogging_velocity)

    # Assert if motor is homed
    def test_TC_1321(self):
        motor = self.motor
        tc_no = "TC-1321"
        self.assertNotEqual(0, self.msta & self.lib.MSTA_BIT_HOMED, 'MSTA.homed (Axis has been homed)')
        self.assertNotEqual(self.llm, self.hlm, 'llm must be != hlm')
        self.assertNotEqual(0, self.jogging_velocity, 'JVEL or VELO must be != 0.0')


    # high limit switch
    def test_TC_1322(self):
        motor = self.motor
        if (self.msta & self.lib.MSTA_BIT_HOMED) and (self.jogging_velocity != 0.0):
            tc_no = "TC-1322-low-limit-switch"
            print '%s' % tc_no
            old_high_limit = epics.caget(motor + '.HLM')
            old_low_limit = epics.caget(motor + '.LLM')
            epics.caput(motor + '.STOP', 1)
            #Go away from limit switch
            self.lib.movePosition(motor, tc_no, self.per30_UserPosition, self.moving_velocity, self.acceleration)
            #switch off the soft limits. Depending on the postion
            # low or high must be set to 0 first
            self.lib.setSoftLimitsOff(motor)

            if (self.saved_JVEL == 0.0) :
                epics.caput(motor + '.JVEL', self.jogging_velocity)
            epics.caput(motor + '.JOGR', 1, wait=True)
            # Get values, check them later
            lvio = int(epics.caget(motor + '.LVIO'))
            mstaE = int(epics.caget(motor + '.MSTA'))
            #Go away from limit switch
            self.lib.movePosition(motor, tc_no, old_high_limit, self.moving_velocity, self.acceleration)
            print '%s msta=%x lvio=%d' % (tc_no, mstaE, lvio)

            self.lib.setSoftLimitsOn(motor, old_low_limit, old_high_limit)
            epics.caput(motor + '.JVEL', self.saved_JVEL)

            #self.assertEqual(0, lvio, 'LVIO == 0')
            self.assertEqual(0, mstaE & self.lib.MSTA_BIT_PROBLEM,    'No Error MSTA.Problem at PLUS_LS')
            self.assertNotEqual(0, mstaE & self.lib.MSTA_BIT_MINUS_LS,   'Minus hard limit switch not active')
            self.assertEqual(0, mstaE & self.lib.MSTA_BIT_PLUS_LS, 'Plus hard limit switch active')


