function v = GRIDDYN_COMPLETE()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 13);
  end
  v = vInitialized;
end
