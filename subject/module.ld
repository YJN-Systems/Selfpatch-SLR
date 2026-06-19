SECTIONS
{
  .spslr_units : {
    __start_spslr_units = .;
    KEEP(*(spslr_units))
    __stop_spslr_units = .;
  }

  .spslr_targets : {
    __start_spslr_targets = .;
    KEEP(*(spslr_targets))
    __stop_spslr_targets = .;
  }
}
INSERT AFTER .data;
